#import <UIKit/UIKit.h>
#import <MetalKit/MetalKit.h>
#import <QuartzCore/QuartzCore.h>
#import <AVFoundation/AVFoundation.h>
#import <CommonCrypto/CommonDigest.h>
#include <sys/utsname.h>

#include "MetalRenderer.hpp"
#include "MetalSupersampling.hpp"
#include "g1/ContactShadow.hpp"
#include "g1/DamageAnchor.hpp"
#include "g1/TileUpscale.hpp"
#include "g1/TacticalModel.hpp"
#include "MetalM3GRenderer.hpp"
#include "g1/M3GBattlePresentation.hpp"
#include "g1/AudioMaintenance.hpp"
#include "g1/MenuMusic.hpp"
#include "g1/SpriteAnimation.hpp"
#include "g1/SupportPresentation.hpp"
#include "g1/SupportDisplayGate.hpp"
#include "g1/SupportOverlayAssets.hpp"
#include "g1/SupportGeometry.hpp"
#include "g1/SupportTutorial.hpp"
#include "g1/TextureEnhancement.hpp"
#include "g1/PresentationEpoch.hpp"
#include "g1/InitialPresentation.hpp"
#include "g1/OriginalUI.hpp"
#include "g1/GameSession.hpp"
#include "g1/SessionSave.hpp"
#include "g1/CampaignController.hpp"
#include "g1/TerrainDecorations.hpp"
#include "g1/TerrainSpriteDecorations.hpp"
#include "g1/SpritePlacement.hpp"
#include "NativeSourceArt.hpp"
#include "NativeTacticalHUD.hpp"
#include "NativeDialogue.hpp"
#include "NativeBattle.hpp"
#include "NativeTechSpecs.hpp"
#include "NativeCampaign.hpp"
#include "AppleAudio.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <ctime>
#include <utility>

namespace {
constexpr char archive_sha[] = "663d2888982093ce66b7811ccb7a6ad3acd37fc02ffa85144775b88c39e83cb4";
constexpr char tables_sha[] = "b870e3d74244741003c8068178e7f58bb5fbe35599599c918e024227463725a8";

void verify_sha(const g1::Bytes& data, const char* expected) {
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256(data.data(), static_cast<CC_LONG>(data.size()), digest);
    constexpr char hex[] = "0123456789abcdef";
    std::string actual;
    for (const auto byte : digest) { actual += hex[byte >> 4]; actual += hex[byte & 15]; }
    if (actual != expected) throw g1::FormatError("Bundled game data failed its SHA256 check");
}

std::array<float, 4> transform(const g1::Matrix4& matrix, std::array<float, 4> point) {
    std::array<float, 4> result{};
    for (std::size_t row = 0; row < 4; ++row)
        for (std::size_t column = 0; column < 4; ++column)
            result[row] += matrix[column * 4 + row] * point[column];
    return result;
}

// Explicit inspection camera. Original camera setup is a separate recovery
// task; this view must not be used as evidence of original camera parity.
struct InspectionCamera {
    float center_x{}, center_z{}, span{5}, base_span{5}, aspect{2};
    float up_y=0.7071067811865476F,up_z=-0.7071067811865476F;
    void set_modern(bool modern) noexcept {
        // Presentation only: 35-degree elevation exposes faces/chests better
        // than the original inspection camera. Classic keeps its prior view.
        up_y=modern?0.81915204429F:0.7071067811865476F;
        up_z=modern?-0.57357643635F:-0.7071067811865476F;
    }

    void fit(const g1::MissionScene& scene, CGSize size) {
        if (size.width <= 0 || size.height <= 0) return;
        aspect = static_cast<float>(size.width / size.height);
        center_x = scene.world.grid.width * 0.16F;
        center_z = scene.world.grid.height * 0.16F;
        base_span = 1.15F * std::max(scene.world.grid.height * 0.32F * -up_z + 0.6F,
                                   (scene.world.grid.width * 0.32F + 0.8F) / aspect);
        span = base_span;
    }

    g1::Matrix4 matrix() const {
        g1::Matrix4 result{};
        result[0] = 2 / (span * aspect);
        result[5] = 2 * up_y / span;
        result[9] = 2 * up_z / span;
        result[6] = up_z / 100;
        result[10] = -up_y / 100;
        result[12] = -center_x * result[0];
        result[13] = -center_z * result[9];
        result[14] = 0.5F - center_z * result[10];
        result[15] = 1;
        return result;
    }
};

struct FrameStats {
    std::deque<double> times;
    std::uint64_t total{};
    void add(double time) {
        if (time <= 0) return;
        if (!times.empty() && time <= times.back()) return;
        if (!times.empty() && time - times.back() > 1) times.clear();
        times.push_back(time);
        if (times.size() > 181) times.pop_front();
        ++total;
    }
    double fps() const {
        return times.size() > 1 ? (times.size() - 1) / (times.back() - times.front()) : 0;
    }
    double interval_percentile(double percentile) const {
        if (times.size() < 2) return 0;
        std::vector<double> values;
        for (std::size_t i = 1; i < times.size(); ++i)
            values.push_back((times[i] - times[i - 1]) * 1000);
        std::sort(values.begin(), values.end());
        return values[static_cast<std::size_t>(percentile * (values.size() - 1))];
    }
};

NSString* native_text(const std::string& value) {
    NSString* text = [[NSString alloc] initWithBytes:value.data() length:value.size()
                                             encoding:NSUTF8StringEncoding];
    if (!text) throw g1::FormatError("Original localized text is not valid UTF8");
    return text;
}

// Original source-pack UI icons are preserved as pixel-for-pixel PNG bytes;
// UIKit sees them at native Retina scale, never as AI-generated replacements.
UIImage* original_g1_ui_png(const g1::Archive& archive,std::uint32_t id,
                          std::uint32_t width,std::uint32_t height,CGFloat scale=2.0) {
    const auto ids=archive.find(id,0xb7178678U);
    if(ids.size()!=1)throw g1::FormatError("Original game UI asset is missing or ambiguous");
    const auto bytes=archive.read(ids.front());
    const auto pixels=g1::decode_png(bytes);
    if(pixels.width!=width || pixels.height!=height)
        throw g1::FormatError("Original game UI asset dimensions differ from source");
    UIImage* source=[UIImage imageWithData:[NSData dataWithBytes:bytes.data() length:bytes.size()]];
    if(!source.CGImage)throw g1::FormatError("Original game UI bitmap failed UIKit decode");
    UIImage* enhanced=g1::adaptive_ui::image(pixels);
    return [UIImage imageWithCGImage:enhanced.CGImage scale:4.0*scale orientation:UIImageOrientationUp];
}

UIButton* action_button(NSString* title, id target, SEL selector) {
    UIButton* button = [UIButton buttonWithType:UIButtonTypeSystem];
    [button setTitle:title forState:UIControlStateNormal];
    [button setTitleColor:UIColor.whiteColor forState:UIControlStateNormal];
    [button setTitleColor:[UIColor colorWithWhite:0.5 alpha:1] forState:UIControlStateDisabled];
    button.titleLabel.font = [UIFont fontWithName:@"Menlo-Bold" size:13] ?: [UIFont boldSystemFontOfSize:13];
    button.backgroundColor = [UIColor colorWithRed:0.18 green:0.024 blue:0.045 alpha:0.96];
    button.layer.borderWidth = 1.0;
    button.layer.borderColor = [UIColor colorWithRed:0.68 green:0.22 blue:0.26 alpha:1].CGColor;
    button.layer.cornerRadius = 3;
    [button addTarget:target action:selector forControlEvents:UIControlEventTouchUpInside];
    [button.widthAnchor constraintGreaterThanOrEqualToConstant:82].active = YES;
    [button.heightAnchor constraintEqualToConstant:40].active = YES;
    return button;
}

// The 52x49 source action icon is the complete visible control; its name is
// kept as a VoiceOver label, not drawn beside original G1 artwork. No UIKit
// button backplate, simulated texture or replacement illustration is added.
UIButton* original_action_button(NSString* label, id target, SEL selector, CGFloat width=52) {
    UIButton* button=[UIButton buttonWithType:UIButtonTypeCustom];
    button.isAccessibilityElement=YES;
    button.accessibilityLabel=label;
    button.accessibilityHint=@"Tap to perform this action when available.";
    button.adjustsImageWhenDisabled=NO;
    button.adjustsImageWhenHighlighted=NO;
    button.contentEdgeInsets=UIEdgeInsetsZero;
    button.imageView.contentMode=UIViewContentModeScaleAspectFit;
    [button addTarget:target action:selector forControlEvents:UIControlEventTouchUpInside];
    [button.widthAnchor constraintEqualToConstant:width].active=YES;
    [button.heightAnchor constraintEqualToConstant:49].active=YES;
    return button;
}

void update_original_action_state(UIButton* button, BOOL active) {
    // Source inactive state has its OWN PNG. No translucent fake outline.
    button.alpha=1.0;
    button.layer.borderWidth=0.0;
    (void)active;
}

g1::Matrix4 unit_model(const g1::WorldBootstrap& world, const g1::MovementUnitState& unit) {
    bool on_site = false, in_group = false;
    const auto matches = [&](g1::WorldCoordinate p) {
        return p.x < 128 && p.y < 128 && p.x == unit.tile_x && p.y == unit.tile_y;
    };
    for (const auto& site : world.sites) {
        on_site = on_site || matches(site.position);
        for (const auto& group : site.coordinate_groups)
            in_group = in_group || std::any_of(group.begin(),group.end(),matches);
    }
    return g1::make_unit_sprite_placement({unit.tile_x,unit.tile_y,unit.form,
        unit.offset_x,unit.offset_y,on_site,in_group}).model;
}

bool inside_triangle(CGPoint p, CGPoint a, CGPoint b, CGPoint c) {
    const auto side = [](CGPoint x, CGPoint y, CGPoint z) {
        return (x.x-z.x)*(y.y-z.y)-(y.x-z.x)*(x.y-z.y);
    };
    const double d1=side(p,a,b),d2=side(p,b,c),d3=side(p,c,a);
    return !((d1 < -0.01 || d2 < -0.01 || d3 < -0.01) &&
             (d1 > 0.01 || d2 > 0.01 || d3 > 0.01));
}

struct NativeCameraPan {
    std::uint64_t token{};
    bool session_focus{};
    float from_x{}, from_z{}, to_x{}, to_z{};
    std::uint32_t elapsed{}, duration{360};
    bool reached{};
};

struct NativeTransformCompletion {
    std::size_t unit_index{};
    std::uint64_t transform_generation{}, spawn_generation{};
};

struct NativePresentedFrame {
    using Support=g1::SupportFrameReceipt;
    struct Battle {
        std::uint64_t token{},revision{};
        bool complete{};
        std::size_t queued_draws{};
    };
    std::uint64_t scene_revision{}, content_generation{}, presentation_epoch{};
    std::optional<std::size_t> mission_camera;
    std::optional<std::uint64_t> session_focus;
    std::optional<std::uint64_t> world;
    std::vector<NativeTransformCompletion> transforms;
    std::optional<Battle> battle;
    std::optional<Support> support;
};

// Both asynchronous Metal facts are required before any core acknowledgement.
// These members are touched only by blocks dispatched to the main queue.
struct NativePresentationDelivery {
    std::shared_ptr<const NativePresentedFrame> frame;
    double presented_time{};
    bool completed{},failed{},delivered{};
};

struct NativeWorldOverlay {
    std::shared_ptr<g1::MetalMesh> mesh;
    std::array<float,4> tint;
};
enum class NativePanel { none, startup, pause, results, ending, menu, mode_select, mission_select, settings, help, help_topic, credits, save_error, load_error, message, arena, arena_opponent, tutorial_save_error, tech_specs };
} // namespace

@interface G1ViewController : UIViewController <MTKViewDelegate,UIAdaptivePresentationControllerDelegate>
- (void)installSession:(std::unique_ptr<g1::GameSession>)session;
- (void)rebuildScene:(BOOL)resetPlayback;
- (void)prepareBattle;
- (void)showSupportHelper;
- (void)requestSupportTutorial;
- (void)retrySupportTutorial;
- (void)maintainMusicAfterStep:(g1::MusicMaintenanceSite)site;
- (void)finishBattleFrame:(const NativePresentedFrame::Battle&)frame;
- (void)recordPresentation:(double)time frame:(std::shared_ptr<const NativePresentedFrame>)frame;
- (void)deliverPresentation:(std::shared_ptr<NativePresentationDelivery>)delivery;
- (void)startCampaign:(std::uint8_t)group mission:(std::int32_t)index faction:(std::uint8_t)faction opponent:(std::int8_t)opponent;
- (void)chooseArenaFaction:(std::int32_t)index;
- (void)chooseArenaOpponent:(std::int32_t)index faction:(std::uint8_t)faction;
- (void)chooseCampaign:(std::uint8_t)group;
- (void)maintainMenuMusic;
- (void)selectProductionType:(std::uint8_t)type mode:(g1::ProductionMode)mode
    session:(std::uint64_t)session producer:(std::size_t)producer spawn:(std::uint64_t)spawn;
- (BOOL)productionTileAllowed:(g1::WorldCoordinate)tile;
- (std::optional<std::size_t>)unitAtPoint:(CGPoint)point padding:(CGFloat)padding;
@end

@implementation G1ViewController {
    MTKView* _metalView;
    id<MTLCommandQueue> _queue;
    NSMutableDictionary<NSNumber*, id<MTLTexture>>* _textures;
    NSMutableDictionary<NSNumber*, id<MTLTexture>>* _modernTextures;
    id<MTLTexture> _white;
    G1WorldTitleLabel* _title;
    UILabel* _hint;
    UILabel* _fps;
    UILabel* _worldLegend;
    UIButton* _reportButton;
    UIButton* _resetButton;
    UIButton* _moveButton;
    UIButton* _captureButton;
    UIButton* _transformButton;
    UIButton* _attackButton;
    UIButton* _healButton;
    UIButton* _upgradeButton;
    UIButton* _endTurnButton;
    UIButton* _buildButton;
    UIButton* _deployButton;
    UIButton* _undoButton;
    UIButton* _menuButton;
    UIButton* _panelContinueButton;
    UIButton* _backButton;
    UIButton* _nextButton;
    UIButton* _skipButton;
    UIView* _commands;
    UIView* _dialoguePanel;
    G1WorldTitleLabel* _dialogueHeading;
    G1TacticalHUDCanvas* _gameHudFrame;
    UIImageView* _dialoguePortrait;
    UIImageView* _dialogueBackdrop;
    NSMutableDictionary<NSNumber*,UIImage*>* _dialoguePortraits;
    UIImage* _skipDialogueIcon;
    UIImage* _pauseGameIcon;
    UIImage* _robotTransformIcon;
    UIImage* _robotTransformDisabledIcon;
    UIImage* _vehicleTransformIcon;
    UIImage* _vehicleTransformDisabledIcon;
    BOOL _menuIsSkip;
    G1DialogueCanvas* _dialogueCanvas;
    UIView* _battlePanel;
    G1BattleCanvas* _battleCanvas;
    UIView* _techSpecsPanel;
    G1TechSpecsCanvas* _techSpecsCanvas;
    G1TechSpecsModelView* _techSpecsModel;
    UIButton* _techSpecsHotspot;
    UIButton* _specsBackButton;
    UIButton* _specsPreviousButton;
    UIButton* _specsNextButton;
    UIButton* _specsRobotButton;
    UIButton* _specsVehicleButton;
    NSMutableDictionary<NSNumber*,UIImage*>* _techSpecsPortraits;
    std::vector<std::size_t> _techSpecsRoster;
    std::size_t _techSpecsPosition;
    std::uint8_t _techSpecsPreviewForm;
    BOOL _techSpecsFromPause;
    UIView* _campaignPanel;
    UIScrollView* _campaignScroll;
    G1CampaignCanvas* _campaignCanvas;
    UIStackView* _campaignButtons;
    UITextView* _menuInfoText;
    NSLayoutConstraint* _campaignCanvasHeight;
    NSLayoutConstraint* _campaignActionsMenuCenter;
    NSLayoutConstraint* _campaignActionsNormalBottom;
    NSLayoutConstraint* _campaignActionsWidth;
    NSLayoutConstraint* _menuInfoWidth;
    NSLayoutConstraint* _menuInfoHeight;
    NSLayoutConstraint* _menuInfoCenterY;
    AVPlayer* _openingPlayer;
    AVPlayerLayer* _openingLayer;
    UIView* _openingView;
    BOOL _openingVisible;
    G1AppleAudio* _audio;
    NSTimer* _menuMusicTimer;
    std::shared_ptr<const g1::Archive> _archive;
    g1::Bytes _terrainTableBytes;
    std::unique_ptr<g1::MetalRenderer> _renderer;
    std::unique_ptr<g1::MetalSupersampling> _supersampling;
    g1::quality::Plan _renderQuality;
    bool _renderMemoryPressure;
    id<MTLTexture> _contactShadowTexture;
    std::shared_ptr<g1::MetalMesh> _contactShadowMesh;
    std::unique_ptr<g1::MetalM3GRenderer> _m3gRenderer;
    std::unique_ptr<g1::M3GBattlePresentation> _battlePresentation;
    std::unique_ptr<g1::SupportOverlayAssets> _supportAssets;
    std::unique_ptr<g1::SupportPresentation> _supportPresentation;
    id<MTLTexture> _supportTexture;
    std::vector<g1::MetalDraw> _supportUnitDraws;
    std::shared_ptr<g1::MetalMesh> _supportCursor;
    std::optional<g1::WorldCoordinate> _supportSavedCursor;
    NSString* _supportSavedTitle;
    g1::SupportDisplayGate _supportDisplay;
    std::uint64_t _supportTicks,_supportPresentedFrames,_supportSequences;
    std::uint64_t _supportTutorialRequests;
    bool _supportTutorialRetryRequired;
    std::vector<std::shared_ptr<g1::MetalM3GModel>> _battleModels;
    std::unique_ptr<g1::MissionScene> _scene;
    std::unique_ptr<g1::GameSession> _session;
    std::unique_ptr<g1::CampaignController> _campaign;
    std::unique_ptr<g1::TextTable> _text;
    std::shared_ptr<g1::MetalMesh> _terrain, _animatedTerrain, _decorationTerrain, _range, _cursor, _attackRange, _healRange;
    std::vector<NativeWorldOverlay> _worldOverlays;
    std::vector<g1::TerrainQuad> _controlQuads;
    std::vector<std::int32_t> _worldOverlaySignature;
    g1::TerrainTables _terrainTables;
    std::uint64_t _renderedTerrainGeneration,_renderedTerrainWorld;
    std::map<std::uint32_t,std::vector<std::shared_ptr<g1::MetalMesh>>> _terrainSpriteMeshes;
    std::vector<g1::TerrainSpriteDecorationDraw> _terrainSpriteDraws;
    std::vector<std::vector<std::vector<g1::SceneVertex>>> _spriteVertices;
    std::vector<std::vector<std::shared_ptr<g1::MetalMesh>>> _spriteMeshes;
    std::vector<std::optional<g1::SpriteAnimation>> _animations;
    std::vector<std::int32_t> _animationIds;
    std::vector<bool> _renderedActive;
    std::vector<std::uint64_t> _playbackSpawnGenerations, _playbackTransformGenerations;
    std::vector<std::optional<NativeTransformCompletion>> _pendingTransformCompletions;
    std::vector<g1::MovementUnitState> _previousUnits;
    std::vector<std::pair<std::size_t,g1::CompletionPresentation>> _pendingCompletionRequests;
    std::vector<g1::SessionBattle> _completedBattles;
    std::vector<std::size_t> _attackTargets;
    std::vector<std::size_t> _healTargets;
    std::optional<g1::ProductionMode> _productionMode;
    std::optional<std::uint8_t> _productionType;
    std::optional<g1::ProductionPlacement> _productionPlacement;
    std::optional<g1::WorldCoordinate> _returnCursor;
    std::shared_ptr<g1::MetalMesh> _productionRange;
    bool _typeListOpen;
    std::uint64_t _productionActions;
    std::uint64_t _undoActions;
    g1::UnitAnimationTable _animationTable;
    std::vector<g1::Matrix4> _unitModels;
    std::map<std::uint8_t,std::unique_ptr<g1::TacticalModel>> _tacticalModels;
    std::map<std::uint8_t,std::shared_ptr<g1::MetalM3GModel>> _tacticalGpu;
    std::map<std::uint8_t,std::string> _tacticalErrors;
    std::map<std::size_t,CGRect> _tacticalHitBounds;
    std::size_t _tacticalDrawn;
    std::size_t _tacticalFallback;
    std::size_t _battleEnhancedTextures;
    g1::SimulationClock _simulationClock;
    std::optional<NativeCameraPan> _cameraPan;
    std::optional<g1::WorldCoordinate> _pendingDestination;
    std::optional<g1::MovementRange> _previewRange;
    std::optional<std::size_t> _lastSelection;
    std::optional<std::uint64_t> _preparedWorldToken;
    std::optional<g1::MissionAudioRequest> _lastMapMusic;
    g1::Bytes _lastCheckpointBytes;
    NSString* _sessionSavePath;
    NSString* _profilePath;
    NSString* _checkpointError;
    NSString* _campaignError;
    NativePanel _panelPurpose;
    NativePanel _informationReturnPanel;
    std::uint64_t _panelToken;
    std::uint64_t _localPanelToken;
    std::uint32_t _resultsInputEdges;
    std::int32_t _missionPickerPage;
    std::uint8_t _missionPickerGroup;
    std::int32_t _pendingArenaIndex;
    std::uint8_t _pendingArenaFaction;
    std::vector<std::uint32_t> _pendingMessageIds;
    std::vector<std::uint32_t> _presentedMessageIds;
    bool _campaignRetryRequired;
    bool _checkpointRequested, _checkpointRetryRequired, _checkpointRetired;
    bool _restoredCheckpoint, _healMode;
    std::size_t _checkpointCommand;
    InspectionCamera _camera;
    FrameStats _frameStats;
    double _clockOrigin, _lastHudTime;
    std::uint32_t _lastTick;
    std::uint32_t _inputEdges, _randomSeed;
    std::size_t _inputCommand, _dialogueCommand;
    std::uint64_t _simulationTicks, _contentGeneration, _presentedContentChanges;
    std::uint64_t _lastPresentedContent, _cameraReports, _moveActions, _captureActions, _transformActions;
    std::uint64_t _audioRequests, _clampedClockIntervals;
    std::uint64_t _attackActions, _battlePresentationTicks, _battleAcknowledgements;
    std::uint64_t _battlePresentedToken, _battlePreparedFrames, _battlePresentedFrames;
    std::uint64_t _lastBattleFinalToken, _lastBattleFinalRevision, _battleQueuedDrawsPresented;
    std::uint64_t _sceneRevision, _sceneRebuilds, _sessionInstalls;
    g1::PresentationEpoch _presentationEpoch;
    std::uint64_t _focusReports, _transformReports, _spawnPlaybackResets;
    std::uint64_t _worldReports, _checkpointWrites, _checkpointLoads, _checkpointRetirements;
    std::uint64_t _healActions, _upgradeActions, _endTurnActions, _resultsReports, _endingReports;
    std::uint64_t _staleGpuErrors;
    std::uint8_t _lastMissionMode;
    bool _clockStarted, _loaded, _failed, _moveMode, _attackMode, _rangeDirty;
    bool _graphicsModern, _showDiagnostics, _settingsFromPause;
    float _touchCameraSpeed; // initialized from registered defaults in viewDidLoad
    NSString* _failure;
    NSString* _notice;
    double _noticeUntil;
}

- (BOOL)prefersStatusBarHidden { return YES; }
- (BOOL)prefersHomeIndicatorAutoHidden { return YES; }
- (void)viewSafeAreaInsetsDidChange {
    [super viewSafeAreaInsetsDidChange];[self.view setNeedsLayout];
}
- (UIInterfaceOrientationMask)supportedInterfaceOrientations { return UIInterfaceOrientationMaskLandscape; }

- (void)viewDidLoad {
    [super viewDidLoad];
    _dialogueCommand = std::numeric_limits<std::size_t>::max();
    _checkpointCommand = std::numeric_limits<std::size_t>::max();
    _lastMissionMode = 255;
    self.view.backgroundColor = [UIColor colorWithRed:0.025 green:0.04 blue:0.065 alpha:1];
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    _metalView = [[MTKView alloc] initWithFrame:self.view.bounds device:device];
    _metalView.translatesAutoresizingMaskIntoConstraints = NO;
    _metalView.colorPixelFormat = MTLPixelFormatBGRA8Unorm;
    _metalView.depthStencilPixelFormat = MTLPixelFormatDepth32Float;
    _metalView.autoResizeDrawable = NO;
    _metalView.clearColor = MTLClearColorMake(0.025,0.04,0.065,1);
    _metalView.preferredFramesPerSecond = 60;
    _metalView.paused = YES;
    _metalView.delegate = self;
    [self.view addSubview:_metalView];
    [NSLayoutConstraint activateConstraints:@[
        [_metalView.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
        [_metalView.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor],
        [_metalView.topAnchor constraintEqualToAnchor:self.view.topAnchor],
        [_metalView.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor]]];

    _title = [G1WorldTitleLabel new];
    _title.text = @"G1 Awakening";
    _title.font = [UIFont systemFontOfSize:17 weight:UIFontWeightSemibold];
    _title.textColor = UIColor.whiteColor;
    _hint = [UILabel new];
    _hint.text = @"Loading the first mission…";
    _hint.font = [UIFont systemFontOfSize:12 weight:UIFontWeightMedium];
    _hint.textColor = [UIColor colorWithWhite:0.85 alpha:1];
    _hint.numberOfLines = 2;
    _worldLegend=[UILabel new];
    _worldLegend.font=[UIFont systemFontOfSize:10 weight:UIFontWeightMedium];
    _worldLegend.textColor=[UIColor colorWithWhite:0.75 alpha:1];
    _worldLegend.text=@"□ Site · ◇ Capture point · Gold/cyan: mission markers";
    _gameHudFrame=[G1TacticalHUDCanvas new];
    _gameHudFrame.translatesAutoresizingMaskIntoConstraints=YES;
    [self.view addSubview:_gameHudFrame];
    UIStackView* headings = [[UIStackView alloc] initWithArrangedSubviews:@[_title,_hint,_worldLegend]];
    headings.axis = UILayoutConstraintAxisVertical;
    headings.spacing = 4;
    headings.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:headings];

    _fps = [UILabel new];
    _fps.text = @"Target: 60 Hz · No device measurement yet";
    _fps.font = [UIFont monospacedDigitSystemFontOfSize:11 weight:UIFontWeightRegular];
    _fps.textColor = [UIColor colorWithWhite:0.8 alpha:1];
    _fps.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:_fps];

    _resetButton = [UIButton buttonWithType:UIButtonTypeSystem];
    [_resetButton setTitle:@"Reset view" forState:UIControlStateNormal];
    [_resetButton addTarget:self action:@selector(resetView) forControlEvents:UIControlEventTouchUpInside];
    _reportButton = [UIButton buttonWithType:UIButtonTypeSystem];
    [_reportButton setTitle:@"Test report" forState:UIControlStateNormal];
    [_reportButton addTarget:self action:@selector(shareReport) forControlEvents:UIControlEventTouchUpInside];
    _menuButton=original_action_button(@"Pause menu",self,@selector(openMenu),60);
    _gameHudFrame.hidden=YES;
    _menuButton.hidden=YES;
    _menuButton.translatesAutoresizingMaskIntoConstraints=YES;
    for(NSLayoutConstraint* limit in [_menuButton.constraints copy])
        if(limit.firstAttribute==NSLayoutAttributeWidth || limit.firstAttribute==NSLayoutAttributeHeight)
            limit.active=NO;
    [self.view addSubview:_menuButton];
    _resetButton.translatesAutoresizingMaskIntoConstraints=YES;
    _reportButton.translatesAutoresizingMaskIntoConstraints=YES;
    [self.view addSubview:_resetButton];
    [self.view addSubview:_reportButton];
    UILayoutGuide* safe = self.view.safeAreaLayoutGuide;
    [NSLayoutConstraint activateConstraints:@[
        [headings.leadingAnchor constraintEqualToAnchor:safe.leadingAnchor constant:16],
        [headings.topAnchor constraintEqualToAnchor:safe.topAnchor constant:10],
        [headings.trailingAnchor constraintLessThanOrEqualToAnchor:safe.trailingAnchor constant:-16],
        [_fps.leadingAnchor constraintEqualToAnchor:safe.leadingAnchor constant:16],
        [_fps.bottomAnchor constraintEqualToAnchor:safe.bottomAnchor constant:-10]]];

    _moveButton = original_action_button(@"Move",self,@selector(beginMove));
    _captureButton = original_action_button(@"Capture",self,@selector(capture));
    _transformButton = original_action_button(@"Transform",self,@selector(transformUnit));
    _attackButton = original_action_button(@"Attack",self,@selector(beginAttack));
    _healButton = original_action_button(@"Repair",self,@selector(beginHeal));
    _upgradeButton = original_action_button(@"Upgrade",self,@selector(upgradeUnit));
    _endTurnButton = original_action_button(@"End turn",self,@selector(endTurn),60);
    _buildButton = original_action_button(@"Activate unit",self,@selector(beginProductionOrDeploy));
    _deployButton = original_action_button(@"Activate tape",self,@selector(beginDeploy));
    _undoButton = original_action_button(@"Undo",self,@selector(undoAction));
    _endTurnButton.hidden=NO;
    // Original landscape HUD uses vertical action banks at the sides,
    // plus a centre-top turn button. Source-space frames are assigned in
    // viewDidLayoutSubviews; no ten-button strip obscures the terrain.
    _commands=[G1PassthroughControls new];
    _commands.backgroundColor=UIColor.clearColor;
    _commands.translatesAutoresizingMaskIntoConstraints=YES;
    [self.view addSubview:_commands];
    for(UIButton* action in @[_moveButton,_captureButton,_transformButton,_attackButton,
                            _healButton,_upgradeButton,_endTurnButton,_buildButton,
                            _deployButton,_undoButton]) {
        action.translatesAutoresizingMaskIntoConstraints=YES;
        for(NSLayoutConstraint* limit in [action.constraints copy])
            if(limit.firstAttribute==NSLayoutAttributeWidth || limit.firstAttribute==NSLayoutAttributeHeight)
                limit.active=NO;
        [_commands addSubview:action];
    }
    _dialoguePanel=[UIView new];
    _dialoguePanel.backgroundColor=UIColor.clearColor;
    _dialoguePanel.clipsToBounds=YES;
    _dialoguePanel.translatesAutoresizingMaskIntoConstraints=YES;
    [self.view addSubview:_dialoguePanel];
    _dialogueBackdrop=[UIImageView new];
    _dialogueBackdrop.contentMode=UIViewContentModeScaleToFill;
    _dialogueBackdrop.autoresizingMask=UIViewAutoresizingFlexibleWidth|UIViewAutoresizingFlexibleHeight;
    [_dialoguePanel addSubview:_dialogueBackdrop];
    _dialoguePortrait=[UIImageView new];
    _dialoguePortrait.contentMode=UIViewContentModeScaleAspectFit;
    _dialoguePortrait.translatesAutoresizingMaskIntoConstraints=YES;
    [_dialoguePanel addSubview:_dialoguePortrait];
    _dialogueHeading=[G1WorldTitleLabel new];
    _dialogueHeading.font=[UIFont systemFontOfSize:10 weight:UIFontWeightBold];
    _dialogueHeading.textColor=UIColor.whiteColor;
    _dialogueHeading.translatesAutoresizingMaskIntoConstraints=YES;
    [_dialoguePanel addSubview:_dialogueHeading];
    _dialogueCanvas=[G1DialogueCanvas new];
    _dialogueCanvas.translatesAutoresizingMaskIntoConstraints=YES;
    [_dialoguePanel addSubview:_dialogueCanvas];
    UITapGestureRecognizer* advance=[[UITapGestureRecognizer alloc]
         initWithTarget:self action:@selector(dialogueTap:)];
    [_dialoguePanel addGestureRecognizer:advance];
    _dialoguePanel.isAccessibilityElement=YES;
    _dialoguePanel.accessibilityLabel=@"Dialogue. Double-tap to continue.";
    // Original dialogue uses tap-to-advance plus the top-left fast-forward
    // icon. The previous Back/Continue/Skip UILabel buttons were never part
    // of the source frame; they are deliberately absent from this screen.
    _dialoguePanel.hidden = YES;
    _commands.hidden = YES;

    _battlePanel = [G1PassthroughControls new];
    _battlePanel.backgroundColor = UIColor.clearColor;
    _battlePanel.layer.cornerRadius = 0;
    _battlePanel.layer.borderWidth = 0;
    _battlePanel.layer.borderColor = [UIColor colorWithWhite:0.5 alpha:0.6].CGColor;
    _battlePanel.translatesAutoresizingMaskIntoConstraints = YES;
    [self.view addSubview:_battlePanel];
    _battleCanvas = [G1BattleCanvas new];
    _battleCanvas.userInteractionEnabled=NO;
    _battleCanvas.translatesAutoresizingMaskIntoConstraints = YES;
    [_battlePanel addSubview:_battleCanvas];
    _battlePanel.hidden = YES;

    // Restore the original source 480x320 faction-specific Transformer
    // character Tech Specs interface, isolated from GameSession actions.
    _techSpecsPanel=[UIView new];
    _techSpecsPanel.frame=self.view.bounds;
    _techSpecsPanel.autoresizingMask=UIViewAutoresizingFlexibleWidth|UIViewAutoresizingFlexibleHeight;
    _techSpecsPanel.backgroundColor=[UIColor colorWithRed:.012 green:.018 blue:.034 alpha:1];
    _techSpecsPanel.hidden=YES;
    [self.view addSubview:_techSpecsPanel];
    _techSpecsCanvas=[G1TechSpecsCanvas new];
    _techSpecsCanvas.translatesAutoresizingMaskIntoConstraints=YES;
    [_techSpecsPanel addSubview:_techSpecsCanvas];
    _techSpecsModel=[[G1TechSpecsModelView alloc] initWithMetalDevice:device];
    _techSpecsModel.translatesAutoresizingMaskIntoConstraints=YES;
    [_techSpecsCanvas addSubview:_techSpecsModel];
    const auto installTechButton=[&](NSString* label,SEL action) {
        UIButton* button=[UIButton buttonWithType:UIButtonTypeCustom];
        button.accessibilityLabel=label;button.isAccessibilityElement=YES;
        button.backgroundColor=UIColor.clearColor;
        [button addTarget:self action:action forControlEvents:UIControlEventTouchUpInside];
        [_techSpecsCanvas addSubview:button];return button;
    };
    _specsBackButton=installTechButton(@"Back to game",@selector(closeTechSpecs));
    _specsPreviousButton=installTechButton(@"Previous Transformer",@selector(previousTechSpecs));
    _specsNextButton=installTechButton(@"Next Transformer",@selector(nextTechSpecs));
    _specsRobotButton=installTechButton(@"View robot form",@selector(showTechRobot));
    _specsVehicleButton=installTechButton(@"View alternate form",@selector(showTechVehicle));

    _campaignPanel=[UIView new];
    _campaignPanel.backgroundColor=[UIColor colorWithRed:0.025 green:0.045 blue:0.075 alpha:0.99];
    _campaignPanel.layer.cornerRadius=0; // Source G1 artwork supplies its own frame.
    _campaignPanel.clipsToBounds=YES;
    _campaignPanel.translatesAutoresizingMaskIntoConstraints=NO;
    [self.view addSubview:_campaignPanel];
    _campaignScroll=[UIScrollView new];
    _campaignScroll.translatesAutoresizingMaskIntoConstraints=NO;
    _campaignScroll.contentInsetAdjustmentBehavior=UIScrollViewContentInsetAdjustmentNever;
    [_campaignPanel addSubview:_campaignScroll];
    _campaignCanvas=[G1CampaignCanvas new];
    _campaignCanvas.translatesAutoresizingMaskIntoConstraints=NO;
    [_campaignScroll addSubview:_campaignCanvas];
    _campaignButtons=[UIStackView new];
    _campaignButtons.axis=UILayoutConstraintAxisVertical;
    _campaignButtons.alignment=UIStackViewAlignmentFill;
    _campaignButtons.spacing=4;
    _campaignButtons.translatesAutoresizingMaskIntoConstraints=NO;
    [_campaignPanel addSubview:_campaignButtons];
    _menuInfoText=[UITextView new];
    _menuInfoText.translatesAutoresizingMaskIntoConstraints=NO;
    _menuInfoText.backgroundColor=UIColor.clearColor;
    _menuInfoText.textColor=UIColor.whiteColor;
    _menuInfoText.font=[UIFont systemFontOfSize:12 weight:UIFontWeightMedium];
    _menuInfoText.editable=NO;_menuInfoText.selectable=NO;
    _menuInfoText.textContainerInset=UIEdgeInsetsMake(1,3,1,3);
    _menuInfoText.scrollEnabled=YES;
    _menuInfoText.hidden=YES;
    [_campaignPanel addSubview:_menuInfoText];
    [_campaignPanel bringSubviewToFront:_campaignButtons];
    _campaignCanvasHeight=[_campaignCanvas.heightAnchor constraintEqualToConstant:180];
    _campaignActionsMenuCenter=[_campaignButtons.centerYAnchor constraintEqualToAnchor:_campaignPanel.centerYAnchor constant:4];
    _campaignActionsNormalBottom=[_campaignButtons.bottomAnchor constraintEqualToAnchor:_campaignPanel.bottomAnchor constant:-12];
    _campaignActionsWidth=[_campaignButtons.widthAnchor constraintEqualToConstant:390];
    _menuInfoWidth=[_menuInfoText.widthAnchor constraintEqualToConstant:372];
    _menuInfoHeight=[_menuInfoText.heightAnchor constraintEqualToConstant:63];
    _menuInfoCenterY=[_menuInfoText.centerYAnchor constraintEqualToAnchor:_campaignPanel.centerYAnchor constant:-5];
    [NSLayoutConstraint activateConstraints:@[
        [_campaignPanel.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
        [_campaignPanel.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor],
        [_campaignPanel.topAnchor constraintEqualToAnchor:self.view.topAnchor],
        [_campaignPanel.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor],
        [_campaignScroll.leadingAnchor constraintEqualToAnchor:_campaignPanel.leadingAnchor],
        [_campaignScroll.trailingAnchor constraintEqualToAnchor:_campaignPanel.trailingAnchor],
        [_campaignScroll.topAnchor constraintEqualToAnchor:_campaignPanel.topAnchor],
        [_campaignScroll.bottomAnchor constraintEqualToAnchor:_campaignPanel.bottomAnchor],
        [_campaignCanvas.leadingAnchor constraintEqualToAnchor:_campaignScroll.contentLayoutGuide.leadingAnchor],
        [_campaignCanvas.trailingAnchor constraintEqualToAnchor:_campaignScroll.contentLayoutGuide.trailingAnchor],
        [_campaignCanvas.topAnchor constraintEqualToAnchor:_campaignScroll.contentLayoutGuide.topAnchor],
        [_campaignCanvas.bottomAnchor constraintEqualToAnchor:_campaignScroll.contentLayoutGuide.bottomAnchor],
        [_campaignCanvas.widthAnchor constraintEqualToAnchor:_campaignScroll.frameLayoutGuide.widthAnchor],
        _campaignCanvasHeight,
        [_menuInfoText.centerXAnchor constraintEqualToAnchor:_campaignPanel.safeAreaLayoutGuide.centerXAnchor],
        _menuInfoCenterY,_menuInfoWidth,_menuInfoHeight,
        [_campaignButtons.centerXAnchor constraintEqualToAnchor:_campaignPanel.safeAreaLayoutGuide.centerXAnchor],
        [_campaignButtons.leadingAnchor constraintGreaterThanOrEqualToAnchor:_campaignPanel.leadingAnchor constant:8],
        [_campaignButtons.trailingAnchor constraintLessThanOrEqualToAnchor:_campaignPanel.trailingAnchor constant:-8],
        _campaignActionsWidth]];
    _campaignActionsNormalBottom.active=YES;
    _campaignPanel.hidden=YES;

    _techSpecsHotspot=[UIButton buttonWithType:UIButtonTypeCustom];
    _techSpecsHotspot.backgroundColor=UIColor.clearColor;
    _techSpecsHotspot.isAccessibilityElement=YES;
    _techSpecsHotspot.accessibilityLabel=@"Selected Transformer Tech Specs";
    _techSpecsHotspot.accessibilityHint=@"Opens the original character statistics and 3D model.";
    [_techSpecsHotspot addTarget:self action:@selector(openTechSpecs)
        forControlEvents:UIControlEventTouchUpInside];
    [self.view addSubview:_techSpecsHotspot];
    _techSpecsHotspot.hidden=YES;

    [_metalView addGestureRecognizer:[[UITapGestureRecognizer alloc]
        initWithTarget:self action:@selector(tap:)]];
    [_metalView addGestureRecognizer:[[UIPanGestureRecognizer alloc]
        initWithTarget:self action:@selector(pan:)]];
    [_metalView addGestureRecognizer:[[UIPinchGestureRecognizer alloc]
        initWithTarget:self action:@selector(pinch:)]];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(inactive:)
        name:UIApplicationWillResignActiveNotification object:nil];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(active:)
        name:UIApplicationDidBecomeActiveNotification object:nil];

    try {
        _renderer = std::make_unique<g1::MetalRenderer>(device);
        _supersampling=std::make_unique<g1::MetalSupersampling>(_metalView.device);
        _contactShadowTexture=_renderer->texture(g1::soft_contact_shadow());
        _contactShadowMesh=_renderer->mesh(g1::contact_shadow_vertices());
        _m3gRenderer = std::make_unique<g1::MetalM3GRenderer>(device);
        _queue = [device newCommandQueue];
        if (!_queue) throw std::runtime_error("Metal command queue is unavailable");
        NSString* path = [NSBundle.mainBundle.bundlePath stringByAppendingPathComponent:@"GameData"];
        auto archiveBytes = g1::read_file([[path stringByAppendingPathComponent:@"iphone.big"] UTF8String]);
        auto tableBytes = g1::read_file([[path stringByAppendingPathComponent:@"terrain-tables.bin"] UTF8String],480);
        verify_sha(archiveBytes, archive_sha);
        verify_sha(tableBytes, tables_sha);
        _archive = std::make_shared<const g1::Archive>(std::move(archiveBytes));
        _terrainTableBytes = std::move(tableBytes);
        _terrainTables = g1::decode_terrain_tables(_terrainTableBytes);
        const auto& archive = *_archive;
        _text = std::make_unique<g1::TextTable>(g1::load_text_table(archive));
        [_title loadOriginalFonts:archive];
        [_campaignCanvas loadOriginalFonts:archive];
        [_campaignCanvas loadOriginalMenuArtwork:archive];
        [_battleCanvas loadOriginalFonts:archive];
        [_dialogueHeading loadOriginalFonts:archive];
        [self loadOriginalHUD:archive];
        [_techSpecsCanvas loadOriginalArt:archive];
        _techSpecsPortraits=[NSMutableDictionary new];
        for(const auto info : {g1::source_art::Portrait{731,54,50},
                               g1::source_art::Portrait{878,73,70},
                               g1::source_art::Portrait{670,54,51},
                               g1::source_art::Portrait{646,48,51}})
            _techSpecsPortraits[@(info.id)]=g1::source_art::indexed_bmp(archive,
                info.id,info.width,info.height);
        const auto animationTable = archive.find(309,0xf4e02223U);
        if (animationTable.size() != 1) throw g1::FormatError("Unit animation table is missing");
        _animationTable = g1::decode_unit_animation_table(archive.read(animationTable.front()));
        _randomSeed = static_cast<std::uint32_t>(std::time(nullptr));
        _audio = [[G1AppleAudio alloc] initWithArchive:archive];
        [NSUserDefaults.standardUserDefaults registerDefaults:@{
            @"G1MusicEnabled":@YES,@"G1SoundEnabled":@YES,
            @"G1GraphicsModern":@NO,@"G1DiagnosticsEnabled":@NO,
            @"G1TouchCameraSpeed":@1.0F}];
        _graphicsModern=[NSUserDefaults.standardUserDefaults boolForKey:@"G1GraphicsModern"];
        _showDiagnostics=[NSUserDefaults.standardUserDefaults boolForKey:@"G1DiagnosticsEnabled"];
        const float storedSpeed=[NSUserDefaults.standardUserDefaults floatForKey:@"G1TouchCameraSpeed"];
        _touchCameraSpeed=std::isfinite(storedSpeed) ?
            std::clamp(storedSpeed,0.75F,1.5F) : 1.0F;
        [self updateDiagnosticsVisibility];
        [_audio setMusicEnabled:[NSUserDefaults.standardUserDefaults boolForKey:@"G1MusicEnabled"]
            soundEnabled:[NSUserDefaults.standardUserDefaults boolForKey:@"G1SoundEnabled"]];
        for (std::int32_t group=0;group<3;++group)
            [_audio request:g1::MissionAudioRequest{0,group}];
        // UIKit menus also exist before the Metal scene/display loop starts.
        // Poll the real any-voice state at the existing 30-ms simulation step;
        // this never schedules a loop or guesses an audio duration.
        __weak G1ViewController* weakSelf=self;
        _menuMusicTimer=[NSTimer timerWithTimeInterval:static_cast<NSTimeInterval>(g1::SimulationClock::step_ms)/1000.0
            repeats:YES block:^(NSTimer* timer){(void)timer;[weakSelf maintainMenuMusic];}];
        [NSRunLoop.mainRunLoop addTimer:_menuMusicTimer forMode:NSRunLoopCommonModes];
        [self.view layoutIfNeeded];
        _openingVisible=YES; // Defer source menu music while the user's intro plays.
        [self loadCampaignMenu];
        [self startOpening];
    } catch (const std::exception& error) {
        [self fail:[NSString stringWithUTF8String:error.what()]];
    }
}

// A native 64-bit media pipeline plays the supplied replacement cinematic.
// This contains no legacy Glu splash or original ARM32 startup code.
- (void)startOpening {
    NSURL* url=[NSBundle.mainBundle URLForResource:@"G1OpeningXR" withExtension:@"mp4"];
    if(!url) {
        _openingVisible=NO;
        throw g1::FormatError("The replacement G1 opening movie is missing from the IPA");
    }
    _openingPlayer=[AVPlayer playerWithURL:url];
    if(!_openingPlayer || !_openingPlayer.currentItem) {
        _openingVisible=NO;
        throw g1::FormatError("Native AVPlayer could not open the replacement G1 intro");
    }
    _openingPlayer.actionAtItemEnd=AVPlayerActionAtItemEndPause;
    _openingView=[[UIView alloc] initWithFrame:self.view.bounds];
    _openingView.autoresizingMask=UIViewAutoresizingFlexibleWidth|UIViewAutoresizingFlexibleHeight;
    _openingView.backgroundColor=UIColor.blackColor;
    _openingLayer=[AVPlayerLayer playerLayerWithPlayer:_openingPlayer];
    _openingLayer.frame=_openingView.bounds;
    _openingLayer.videoGravity=AVLayerVideoGravityResizeAspect;
    [_openingView.layer addSublayer:_openingLayer];
    [_openingView addGestureRecognizer:[[UITapGestureRecognizer alloc]
        initWithTarget:self action:@selector(skipOpening:)]];
    _openingView.isAccessibilityElement=YES;
    _openingView.accessibilityLabel=@"G1 Awakening opening cinematic. Double tap to skip.";
    [self.view addSubview:_openingView];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(openingDidStop:)
        name:AVPlayerItemDidPlayToEndTimeNotification object:_openingPlayer.currentItem];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(openingDidStop:)
        name:AVPlayerItemFailedToPlayToEndTimeNotification object:_openingPlayer.currentItem];
    [_openingPlayer play];
}
- (void)skipOpening:(UITapGestureRecognizer*)gesture {
    if(gesture.state==UIGestureRecognizerStateEnded)[self finishOpening];
}
- (void)openingDidStop:(NSNotification*)event {
    (void)event;[self finishOpening];
}
- (void)finishOpening {
    if(!_openingVisible)return;
    _openingVisible=NO;
    if(_openingPlayer.currentItem)
        [[NSNotificationCenter defaultCenter] removeObserver:self name:AVPlayerItemDidPlayToEndTimeNotification
            object:_openingPlayer.currentItem];
    if(_openingPlayer.currentItem)
        [[NSNotificationCenter defaultCenter] removeObserver:self name:AVPlayerItemFailedToPlayToEndTimeNotification
            object:_openingPlayer.currentItem];
    [_openingPlayer pause];
    [_openingPlayer replaceCurrentItemWithPlayerItem:nil];
    UIView* view=_openingView;
    _openingPlayer=nil;_openingLayer=nil;_openingView=nil;
    [UIView animateWithDuration:0.20 animations:^{view.alpha=0;} completion:^(BOOL completed){
        (void)completed;[view removeFromSuperview];
    }];
    [self maintainMenuMusic];
}
- (void)loadOriginalHUD:(const g1::Archive&)archive {
    [_gameHudFrame loadOriginalArt:archive];
    _dialogueBackdrop.image=[original_g1_ui_png(archive,1034,480,93,1.0)
        resizableImageWithCapInsets:UIEdgeInsetsMake(12,174,8,155) resizingMode:UIImageResizingModeTile];
    _dialoguePortraits=[NSMutableDictionary new];
    for(const auto portrait : {g1::source_art::Portrait{1295,77,66},
                               g1::source_art::Portrait{396,70,66},
                               g1::source_art::Portrait{878,73,70},
                               g1::source_art::Portrait{1156,76,68}})
        _dialoguePortraits[@(portrait.id)]=g1::source_art::indexed_bmp(archive,
            portrait.id,portrait.width,portrait.height);
    _battlePanel.backgroundColor=UIColor.clearColor;
    // Native source uses distinct enabled/disabled art, not alpha-faded
    // replicas. Every pair is read directly from the original FGIB PNG bank.
    struct SourceButtonArt { UIButton* button; std::uint32_t enabled;std::uint32_t disabled; };
    const std::array<SourceButtonArt,10> mapped{{
        {_moveButton,1118,721}, {_captureButton,919,633},
        {_transformButton,904,433}, {_attackButton,660,850},
        {_healButton,669,726}, {_upgradeButton,1118,721},
        {_endTurnButton,454,454}, {_buildButton,442,548},
        {_deployButton,919,633}, {_undoButton,793,1076}}};
    for(const auto& mapping:mapped) {
        UIImage* on=original_g1_ui_png(archive,mapping.enabled,52,49,1.0);
        UIImage* off=original_g1_ui_png(archive,mapping.disabled,52,49,1.0);
        [mapping.button setImage:[on imageWithRenderingMode:UIImageRenderingModeAlwaysOriginal]
                forState:UIControlStateNormal];
        [mapping.button setImage:[off imageWithRenderingMode:UIImageRenderingModeAlwaysOriginal]
                forState:UIControlStateDisabled];
        mapping.button.imageView.contentMode=UIViewContentModeScaleAspectFit;
        mapping.button.contentHorizontalAlignment=UIControlContentHorizontalAlignmentCenter;
        mapping.button.contentVerticalAlignment=UIControlContentVerticalAlignmentCenter;
        mapping.button.accessibilityIdentifier=[NSString stringWithFormat:@"g1-action-%u",mapping.enabled];
    }
    _robotTransformIcon=original_g1_ui_png(archive,591,52,49,1.0);
    _robotTransformDisabledIcon=original_g1_ui_png(archive,1191,52,49,1.0);
    _vehicleTransformIcon=original_g1_ui_png(archive,904,52,49,1.0);
    _vehicleTransformDisabledIcon=original_g1_ui_png(archive,433,52,49,1.0);
    _pauseGameIcon=[original_g1_ui_png(archive,905,60,49,1.0)
        imageWithRenderingMode:UIImageRenderingModeAlwaysOriginal];
    _skipDialogueIcon=[original_g1_ui_png(archive,719,60,49,1.0)
        imageWithRenderingMode:UIImageRenderingModeAlwaysOriginal];
    [_menuButton setImage:_pauseGameIcon forState:UIControlStateNormal];
    [_menuButton setImage:_pauseGameIcon forState:UIControlStateDisabled];
    _menuButton.accessibilityIdentifier=@"g1-original-pause-menu";
}
- (NSString*)originalText:(std::uint16_t)identifier {
    return native_text(_text->at(identifier));
}

- (void)clearCampaignButtons {
    for(UIView* view in [_campaignButtons.arrangedSubviews copy]) {
        [_campaignButtons removeArrangedSubview:view];[view removeFromSuperview];
    }
}

- (UIButton*)addCampaignButton:(NSString*)title action:(SEL)action {
    UIStackView* row=(UIStackView*)_campaignButtons.arrangedSubviews.lastObject;
    const BOOL isSourceMenu=_campaignActionsMenuCenter.active;
    if(!row || row.arrangedSubviews.count>=(isSourceMenu?g1::original_ui::menu_columns:4)) {
        row=[UIStackView new];row.spacing=3;row.distribution=UIStackViewDistributionFillEqually;
        [_campaignButtons addArrangedSubview:row];
    }
    const BOOL originalMenu=[_campaignCanvas originalFaceForMenuText:@"MAIN MENU"] &&
        (_panelPurpose==NativePanel::startup || _panelPurpose==NativePanel::menu ||
         _panelPurpose==NativePanel::mode_select ||
         _panelPurpose==NativePanel::mission_select || _panelPurpose==NativePanel::settings ||
         _panelPurpose==NativePanel::help || _panelPurpose==NativePanel::help_topic ||
         _panelPurpose==NativePanel::credits ||
         _panelPurpose==NativePanel::pause || _panelPurpose==NativePanel::arena ||
         _panelPurpose==NativePanel::arena_opponent);
    UIButton* button=nil;
    if(originalMenu) {
        G1OriginalMenuButton* original=[G1OriginalMenuButton new];
        title=title.uppercaseString;
        [original setTitle:title forState:UIControlStateNormal];
        [original setTitleColor:[UIColor colorWithRed:0.94 green:0.22 blue:0.26 alpha:1]
                        forState:UIControlStateNormal];
        [original setTitleColor:[UIColor colorWithWhite:0.48 alpha:1]
                        forState:UIControlStateDisabled];
        original.titleLabel.font=[UIFont boldSystemFontOfSize:13];
        original.titleLabel.adjustsFontSizeToFitWidth=YES;original.titleLabel.minimumScaleFactor=0.65;
        original.backgroundColor=UIColor.clearColor;
        [original useOriginalFace:[_campaignCanvas originalFaceForMenuText:title]
                             fallback:[_campaignCanvas originalSmallFaceForMenuText:title]];
        [original.heightAnchor constraintEqualToConstant:29].active=YES;
        button=original;
        [button addTarget:self action:action forControlEvents:UIControlEventTouchUpInside];
    } else button=action_button(title,self,action);
    [row addArrangedSubview:button];
    // Source menus must not exceed the recovered 3-row artwork region.
    if(originalMenu && _campaignButtons.arrangedSubviews.count>3)
        NSAssert(NO,@"Original menu choices exceed the artwork's native three-row capacity");
    return button;
}

- (void)updateCampaignLayout {
    if(_campaignPanel.hidden)return;
    const CGFloat width=_campaignScroll.bounds.size.width;
    const CGFloat height=_campaignScroll.bounds.size.height;
    if(width>32 && height>0) {
        const BOOL originalMenu=_campaignActionsMenuCenter.active;
        const CGFloat target=originalMenu ? height :
            std::max(height,[_campaignCanvas heightForWidth:width]);
        if(std::abs(_campaignCanvasHeight.constant-target)>0.5)_campaignCanvasHeight.constant=target;
        // Every touch choice fits inside the untouched red source panel.
        // Preserve original 480:320 art aspect, including on XR.
        const auto screen=g1::adaptive_ui::layout(self.view);
        const auto art=g1::original_ui::fit(screen.safe.width,screen.safe.height);
        if(originalMenu) {
            const CGFloat scale=screen.scale;
            _campaignButtons.spacing=4*scale;
            for(UIStackView* row in _campaignButtons.arrangedSubviews) {
                row.spacing=3*scale;
                for(UIButton* button in row.arrangedSubviews) {
                    if([button isKindOfClass:G1OriginalMenuButton.class]) {
                        [(G1OriginalMenuButton*)button setSourceScale:scale];
                        for(NSLayoutConstraint* c in button.constraints)
                            if(c.firstAttribute==NSLayoutAttributeHeight && !c.secondItem)c.constant=29*scale;
                    }
                }
            }
            _menuInfoText.font=[UIFont systemFontOfSize:12*scale weight:UIFontWeightMedium];
        }

        const CGFloat actionsWidth=originalMenu ?
            std::max(CGFloat(0),CGFloat(screen.safe.width-52*screen.scale)) : width-30;
        if(std::abs(_campaignActionsWidth.constant-actionsWidth)>0.5)
            _campaignActionsWidth.constant=actionsWidth;
        if(originalMenu) {
            const BOOL information=_panelPurpose==NativePanel::help_topic || _panelPurpose==NativePanel::credits;
            _campaignActionsMenuCenter.constant=(screen.safe.y+screen.safe.height/2-height/2)+art.source_offset_from_center(information ?
                g1::original_ui::information_action_center_y : g1::original_ui::choices_center_y);
            _menuInfoCenterY.constant=(screen.safe.y+screen.safe.height/2-height/2)+art.source_offset_from_center(
                g1::original_ui::information_text.y + g1::original_ui::information_text.height/2);
            _menuInfoWidth.constant=std::min(width-16,art.project(g1::original_ui::information_text).width);
            _menuInfoHeight.constant=art.project(g1::original_ui::information_text).height;
        }
    }
}

- (void)showPanel:(NativePanel)purpose title:(NSString*)title body:(NSString*)body token:(std::uint64_t)token {
    const bool changed=_panelPurpose!=purpose || _panelToken!=token;
    if(changed){_supportDisplay.invalidate();_presentationEpoch.invalidate();}
    _panelPurpose=purpose;_panelToken=token;
    const BOOL sourceMenu=
        purpose==NativePanel::startup || purpose==NativePanel::menu ||
        purpose==NativePanel::mode_select ||
        purpose==NativePanel::settings || purpose==NativePanel::help ||
        purpose==NativePanel::help_topic || purpose==NativePanel::credits || purpose==NativePanel::pause ||
        purpose==NativePanel::mission_select || purpose==NativePanel::arena ||
        purpose==NativePanel::arena_opponent;
    _campaignActionsNormalBottom.active=NO;
    _campaignActionsMenuCenter.active=NO;
    if(sourceMenu)_campaignActionsMenuCenter.active=YES;
    else _campaignActionsNormalBottom.active=YES;
    _campaignScroll.scrollEnabled=!sourceMenu;
    const BOOL information=purpose==NativePanel::help_topic || purpose==NativePanel::credits;
    _menuInfoText.hidden=!information;
    _menuInfoText.text=information ? body : @"";
    if(information && changed)[_menuInfoText setContentOffset:CGPointZero animated:NO];
    // Source-space positions and information scroll limits are calculated
    // by updateCampaignLayout after the original 480x320 image is fitted.
    [_campaignCanvas setOriginalMenuArtworkVisible:sourceMenu];
    [_campaignCanvas setDisplaysMenuDetails:sourceMenu && purpose==NativePanel::pause];
    _panelContinueButton=nil;
    _inputEdges=0;_resultsInputEdges=0;_simulationClock.reset();
    if(_session)_previousUnits=_session->units();
    _campaignPanel.hidden=NO;
    _menuButton.hidden=YES;_gameHudFrame.hidden=YES;_techSpecsHotspot.hidden=YES;
    _title.hidden=YES;_hint.hidden=YES;_worldLegend.hidden=YES;_fps.hidden=YES;
    _commands.hidden=YES;_dialoguePanel.hidden=YES;_battlePanel.hidden=YES;
    [_campaignCanvas showTitle:title body:body token:token];
    if(changed)[_campaignScroll setContentOffset:CGPointZero animated:NO];
    [self clearCampaignButtons];
    [self.view layoutIfNeeded];
    [self updateCampaignLayout];
    [self maintainMenuMusic];
}

- (void)maintainMenuMusic {
    if(_failed || _openingVisible || !_audio ||
       UIApplication.sharedApplication.applicationState!=UIApplicationStateActive)return;
    // The original main menu is a root even when entered from Pause. Arena
    // follows map list -> faction -> opponent; only opponent is excluded.
    static const g1::MenuMusicNode mainMenu{}, missionList{&mainMenu}, faction{&missionList};
    static const g1::MenuMusicNode enableSounds{}, pause{}, unitActions{}, opponent{&faction};
    const g1::MenuMusicNode* current=nullptr;
    switch(_panelPurpose) {
    case NativePanel::startup: case NativePanel::menu:
    case NativePanel::mode_select:
    case NativePanel::settings: case NativePanel::help: case NativePanel::help_topic:
    case NativePanel::credits:
        current=&mainMenu;break;
    case NativePanel::mission_select: current=&missionList;break;
    case NativePanel::arena: current=&faction;break;
    case NativePanel::arena_opponent: current=&opponent;break;
    case NativePanel::pause: current=&pause;break;
    default:return; // Gameplay/loading/results/messages have separate owners.
    }
    const g1::MenuMusicContext context{[_audio musicEnabled]!=NO,10,current,
                                      &enableSounds,&pause,&opponent,&unitActions};
    if(!g1::menu_music_queries_playing(context))return;
    ++_audioRequests;
    const bool playing=[_audio request:g1::MissionAudioRequest{6,0}];
    if(!playing)if(const auto request=g1::idle_menu_music(context))[self dispatchAudio:*request];
}

- (void)hidePanel {
    _supportDisplay.invalidate();
    _presentationEpoch.invalidate();
    _panelPurpose=NativePanel::none;_panelToken=0;_resultsInputEdges=0;
    _campaignPanel.hidden=YES;
    _menuButton.hidden=NO;_gameHudFrame.hidden=NO;_techSpecsHotspot.hidden=YES;
    _fps.hidden=!_showDiagnostics;
    _title.hidden=!_showDiagnostics;_hint.hidden=!_showDiagnostics;
    _worldLegend.hidden=!_showDiagnostics;
    _menuInfoText.hidden=YES;
    [_campaignCanvas resetPresentation];
    _inputEdges=0;_simulationClock.reset();_clockStarted=false;
    if(_session)_previousUnits=_session->units();
    if(_loaded && !_failed)_metalView.paused=NO;
}

- (void)loadCampaignMenu {
    try {
        if(!_profilePath) {
            NSURL* parent=[NSFileManager.defaultManager URLsForDirectory:NSApplicationSupportDirectory
                inDomains:NSUserDomainMask].firstObject;
            if(!parent)throw std::runtime_error("Application Support directory is unavailable");
            NSURL* folder=[parent URLByAppendingPathComponent:@"G1AwakeningNative" isDirectory:YES];
            NSError* error=nil;
            if(![NSFileManager.defaultManager createDirectoryAtURL:folder
                withIntermediateDirectories:YES attributes:nil error:&error])
                throw std::runtime_error(error.localizedDescription.UTF8String ?: "Cannot create native save directory");
            _profilePath=[[folder URLByAppendingPathComponent:@"campaign.g1native"] path];
            _sessionSavePath=[[folder URLByAppendingPathComponent:@"session.g1native"] path];
        }
        _campaign=std::make_unique<g1::CampaignController>(_profilePath.UTF8String);
        _campaignError=nil;
        [self showProfileMenu];
    } catch(const std::exception& e) {
        _campaignError=native_text(e.what());
        [self showPanel:NativePanel::load_error title:@"Campaign profile could not be loaded"
            body:_campaignError token:++_localPanelToken];
        [self addCampaignButton:@"Retry" action:@selector(loadCampaignMenu)];
    }
}

- (void)showProfileMenu {
    if(!_campaign)return;
    const auto& profile=_campaign->profile();
    NSString* body=[NSString stringWithFormat:@"%@ · %@ %d / %zu\n%@\n%@",
        [self originalText:617],[self originalText:1242],profile.highest_unlocked[0]+1,g1::campaign_mission_count(0),
        profile.completed[0] ? [NSString stringWithFormat:@"%@ · %@ %d / %zu",[self originalText:1151],
            [self originalText:1242],profile.highest_unlocked[1]+1,g1::campaign_mission_count(1)] : [self originalText:846],
        profile.completed[1] ? [self originalText:1190] : [self originalText:527]];
    const bool requested=_campaign->state().gate==g1::CampaignControllerGate::campaign_menu;
    const auto token=requested ? _campaign->state().token : ++_localPanelToken;
    [self showPanel:requested ? NativePanel::menu : NativePanel::startup
        title:[self originalText:1169] body:body token:token];
    if([NSFileManager.defaultManager fileExistsAtPath:_sessionSavePath])
        [self addCampaignButton:[self originalText:771] action:@selector(continueCheckpoint)];
    [self addCampaignButton:[self originalText:471] action:@selector(showModesMenu)];
    [self addCampaignButton:[self originalText:468] action:@selector(openSettings)];
    [self addCampaignButton:[self originalText:754] action:@selector(showHelpMenu)];
    [self addCampaignButton:@"CREDITS" action:@selector(showCreditsMenu)];
    if(_session && _campaign->state().gate==g1::CampaignControllerGate::idle)
        [self addCampaignButton:[self originalText:1205] action:@selector(resumeGame)];
}

- (void)showModesMenu {
    if(!_campaign)return;
    const auto& profile=_campaign->profile();
    NSString* body=[NSString stringWithFormat:@"%@: %d / %zu. %@",
        [self originalText:617],profile.highest_unlocked[0]+1,
        g1::campaign_mission_count(0),profile.completed[0] ? @"SHOWDOWN UNLOCKED" : @"SHOWDOWN LOCKED"];
    [self showPanel:NativePanel::mode_select title:[self originalText:983]
        body:body token:++_localPanelToken];
    [self addCampaignButton:[self originalText:617] action:@selector(chooseStoryCampaign)];
    [self addCampaignButton:[self originalText:1151] action:@selector(chooseShowdown)].enabled=profile.completed[0];
    [self addCampaignButton:[self originalText:1190] action:@selector(chooseArena)].enabled=profile.completed[1];
    [self addCampaignButton:@"Back" action:@selector(showProfileMenu)];
}

- (BOOL)retireCheckpoint {
    if(!_sessionSavePath || ![NSFileManager.defaultManager fileExistsAtPath:_sessionSavePath]) {
        _lastCheckpointBytes.clear();_checkpointRetired=true;return YES;
    }
    NSString* backup=[[_sessionSavePath stringByDeletingLastPathComponent]
        stringByAppendingPathComponent:[NSString stringWithFormat:@"session-%@.retired",NSUUID.UUID.UUIDString]];
    NSError* error=nil;
    if(![NSFileManager.defaultManager moveItemAtPath:_sessionSavePath toPath:backup error:&error]) {
        _checkpointError=error.localizedDescription ?: @"The previous checkpoint could not be retired.";
        return NO;
    }
    ++_checkpointRetirements;
    _checkpointRetired=true;_lastCheckpointBytes.clear();_checkpointError=nil;
    return YES;
}

- (void)saveCheckpoint {
    if(!_checkpointRequested || _checkpointRetryRequired || !_sessionSavePath || !_session ||
       !_session->checkpoint_available())return;
    try {
        const auto bytes=g1::encode_session_checkpoint(_session->make_checkpoint());
        if(bytes!=_lastCheckpointBytes || ![NSFileManager.defaultManager fileExistsAtPath:_sessionSavePath]) {
            const auto written=g1::write_native_save_atomic(_sessionSavePath.UTF8String,bytes);
            if(!written.succeeded)throw std::runtime_error(written.error);
            _lastCheckpointBytes=bytes;++_checkpointWrites;
        }
        _checkpointCommand=_session->mission().command_index;
        _checkpointRequested=false;_checkpointRetired=false;_checkpointError=nil;
    } catch(const std::exception& e) {
        _checkpointError=native_text(e.what());_checkpointRetryRequired=true;
        _notice=@"Checkpoint could not be saved. Open Menu to retry.";_noticeUntil=CACurrentMediaTime()+8;
    }
}

- (void)continueCheckpoint {
    try {
        auto bytes=g1::read_file(_sessionSavePath.UTF8String,g1::max_session_save_size);
        const auto saved=g1::decode_session_checkpoint(*_archive,bytes,&_terrainTables);
        auto session=g1::GameSession::restore_checkpoint(*_archive,saved);
        auto campaign=std::make_unique<g1::CampaignController>(_profilePath.UTF8String);
        [self installSession:std::move(session)];
        _campaign=std::move(campaign);
        _lastCheckpointBytes=std::move(bytes);_checkpointError=nil;_checkpointRetryRequired=false;
        _checkpointRetired=false;_restoredCheckpoint=true;++_checkpointLoads;
        [self hidePanel];[self refreshControls];
    } catch(const std::exception& e) {
        _checkpointError=native_text(e.what());
        [self showPanel:NativePanel::load_error title:@"Checkpoint could not be loaded"
            body:[NSString stringWithFormat:@"%@\n\nThe saved file has been kept.",_checkpointError]
            token:++_localPanelToken];
        [self addCampaignButton:@"Retry" action:@selector(continueCheckpoint)];
        [self addCampaignButton:[self originalText:883] action:@selector(showProfileMenu)];
    }
}

- (void)chooseStoryCampaign {
    [self chooseCampaign:0];
}
- (void)chooseShowdown { if(_campaign && _campaign->profile().completed[0])[self chooseCampaign:1]; }
- (void)chooseArena { if(_campaign && _campaign->profile().completed[1])[self chooseCampaign:2]; }

- (void)chooseCampaign:(std::uint8_t)group {
    if(!_campaign || group>2)return;
    _missionPickerGroup=group;
    _missionPickerPage=0;
    [self showMissionPicker];
}
- (void)showMissionPicker {
    if(!_campaign || _missionPickerGroup>2)return;
    const auto group=_missionPickerGroup;
    const auto count=static_cast<std::int32_t>(g1::campaign_mission_count(group));
    const auto maximum=group==2 ? count-1 :
        std::min(count-1,_campaign->profile().highest_unlocked.at(group));
    const auto start=_missionPickerPage*g1::original_ui::mission_page_size;
    if(start<0 || start>maximum)return;
    NSString* title=[self originalText:group==0?617:group==1?1151:1190];
    NSString* body=[NSString stringWithFormat:
        @"Select an unlocked mission. Page %d of %d. Starting a mission replaces Continue only after the new session has installed.",
        int(_missionPickerPage+1),int(g1::original_ui::mission_page_count(maximum+1))];
    [self showPanel:NativePanel::mission_select title:title body:body token:++_localPanelToken];
    for(std::int32_t index=start;index<=maximum &&
        index<start+g1::original_ui::mission_page_size;++index) {
        NSString* label=[NSString stringWithFormat:@"%@ %d",[self originalText:group==2?536:1242],index+1];
        UIButton* button=[self addCampaignButton:label action:@selector(selectMissionFromButton:)];
        button.tag=index;
    }
    if(_missionPickerPage>0)[self addCampaignButton:@"Previous" action:@selector(previousMissionPage)];
    if(start+g1::original_ui::mission_page_size<=maximum)[self addCampaignButton:@"Next" action:@selector(nextMissionPage)];
    [self addCampaignButton:@"Back" action:@selector(showModesMenu)];
}
- (void)selectMissionFromButton:(UIButton*)button {
    if(_panelPurpose!=NativePanel::mission_select || !button ||
       _missionPickerGroup>2 || !_campaign)return;
    const auto group=_missionPickerGroup;
    const auto index=static_cast<std::int32_t>(button.tag);
    const auto count=static_cast<std::int32_t>(g1::campaign_mission_count(group));
    const auto maximum=group==2 ? count-1 :
        std::min(count-1,_campaign->profile().highest_unlocked.at(group));
    if(index<0 || index>maximum ||
       index/g1::original_ui::mission_page_size!=_missionPickerPage)return;
    if(group==2)[self chooseArenaFaction:index];
    else [self startCampaign:group mission:index faction:0 opponent:1];
}
- (void)previousMissionPage {
    if(_panelPurpose!=NativePanel::mission_select || _missionPickerPage<=0)return;
    --_missionPickerPage;[self showMissionPicker];
}
- (void)nextMissionPage {
    if(_panelPurpose!=NativePanel::mission_select || !_campaign)return;
    const auto group=_missionPickerGroup;
    if(group>2)return;
    const auto maximum=group==2 ?
        static_cast<std::int32_t>(g1::campaign_mission_count(2))-1 :
        std::min(static_cast<std::int32_t>(g1::campaign_mission_count(group))-1,
                 _campaign->profile().highest_unlocked.at(group));
    if((_missionPickerPage+1)*g1::original_ui::mission_page_size>maximum)return;
    ++_missionPickerPage;[self showMissionPicker];
}

- (void)chooseArenaFaction:(std::int32_t)index {
    _pendingArenaIndex=index;
    [self showPanel:NativePanel::arena title:[self originalText:1022]
        body:@"" token:++_localPanelToken];
    [self addCampaignButton:[self originalText:501] action:@selector(arenaAutobots)];
    [self addCampaignButton:[self originalText:1315] action:@selector(arenaDecepticons)];
    [self addCampaignButton:@"Back" action:@selector(showMissionPicker)];
}

- (void)chooseArenaOpponent:(std::int32_t)index faction:(std::uint8_t)faction {
    _pendingArenaIndex=index;_pendingArenaFaction=faction;
    [self showPanel:NativePanel::arena_opponent title:[self originalText:1313]
        body:@"" token:++_localPanelToken];
    [self addCampaignButton:[self originalText:580] action:@selector(arenaComputer)];
    [self addCampaignButton:[self originalText:690] action:@selector(arenaHuman)];
    [self addCampaignButton:@"Back" action:@selector(returnToArenaFaction)];
}
- (void)returnToArenaFaction { [self chooseArenaFaction:_pendingArenaIndex]; }
- (void)arenaAutobots { [self chooseArenaOpponent:_pendingArenaIndex faction:0]; }
- (void)arenaDecepticons { [self chooseArenaOpponent:_pendingArenaIndex faction:1]; }
- (void)arenaComputer { [self startCampaign:2 mission:_pendingArenaIndex faction:_pendingArenaFaction opponent:1]; }
- (void)arenaHuman { [self startCampaign:2 mission:_pendingArenaIndex faction:_pendingArenaFaction opponent:0]; }

- (void)startCampaign:(std::uint8_t)group mission:(std::int32_t)index faction:(std::uint8_t)faction opponent:(std::int8_t)opponent {
    try {
        if(!_campaign || group>2 || index<0 || std::size_t(index)>=g1::campaign_mission_count(group) ||
           (group<2 && index>_campaign->profile().highest_unlocked[group]) ||
           (group==1 && !_campaign->profile().completed[0]) || (group==2 && !_campaign->profile().completed[1]) ||
           faction>1 || opponent<0 || opponent>1 || (group<2 && (faction!=0 || opponent!=1)))
            throw g1::FormatError("Campaign mission has not been unlocked in the saved profile");
        g1::CampaignContext context;context.group=group;context.mission_index=index;
        context.player_factions={{faction,static_cast<std::uint8_t>(1-faction)}};
        context.player_controllers={{0,opponent}};
        _randomSeed=static_cast<std::uint32_t>(std::time(nullptr));
        const g1::GameRandom random(_randomSeed);
        auto bootstrap=g1::bootstrap_campaign(*_archive,context,random,false,&_terrainTables);
        auto session=std::make_unique<g1::GameSession>(*_archive,std::move(bootstrap),context);
        auto campaign=std::make_unique<g1::CampaignController>(_profilePath.UTF8String);
        [self installSession:std::move(session)];
        // Resource creation can throw. Keep Continue available until the
        // replacement scene has actually been installed successfully.
        if(![self retireCheckpoint])throw std::runtime_error(_checkpointError.UTF8String ?: "Checkpoint retirement failed");
        _campaign=std::move(campaign);
        _restoredCheckpoint=false;_checkpointRequested=true;_checkpointRetryRequired=false;
        [self hidePanel];[self refreshControls];
    } catch(const std::exception& e) {
        _campaignError=native_text(e.what());
        [self showPanel:NativePanel::load_error title:@"Mission could not be started"
            body:_campaignError token:++_localPanelToken];
        [self addCampaignButton:[self originalText:883] action:@selector(showProfileMenu)];
    }
}

- (void)openMenu {
    if(_typeListOpen)return;
    if(!_session || _failed) { [self showProfileMenu];return; }
    if(_panelPurpose!=NativePanel::none)return;
    _checkpointRequested=true;[self saveCheckpoint];
    [self showPauseMenu];
}

- (void)showPauseMenu {
    NSString* detail=_checkpointError ? [NSString stringWithFormat:@"Checkpoint: %@",_checkpointError] :
        _lastCheckpointBytes.empty() ? @"A checkpoint will be saved at the next completed objective boundary." :
        @"Continue restores the last completed action at an active objective.";
    [self showPanel:NativePanel::pause title:[self originalText:1162] body:detail token:++_localPanelToken];
    [self addCampaignButton:[self originalText:1205] action:@selector(resumeGame)];
    if(_checkpointRetryRequired)[self addCampaignButton:@"Retry save" action:@selector(retryCheckpoint)];
    [self addCampaignButton:[self originalText:1169] action:@selector(showProfileMenu)];
    [self addCampaignButton:@"Settings" action:@selector(openSettings)];
    [self addCampaignButton:@"Help" action:@selector(showHelpMenu)];
    if(_session && !_session->world().units.empty())
        [self addCampaignButton:@"TECH SPECS" action:@selector(openTechSpecs)];
}

- (void)resumeGame { if(_session && !_failed){[self hidePanel];[self refreshControls];} }
- (void)retryCheckpoint {
    _checkpointRetryRequired=false;_checkpointRequested=true;[self saveCheckpoint];[self showPauseMenu];
}
- (void)updateDiagnosticsVisibility {
    const BOOL display=_showDiagnostics && _panelPurpose==NativePanel::none;
    _fps.hidden=!display;
    _worldLegend.hidden=!display;
    _title.hidden=!display;
    _hint.hidden=!display;
    _reportButton.hidden=!display;
    _resetButton.hidden=!display;
}

- (void)openSettings {
    _settingsFromPause=_panelPurpose==NativePanel::pause;
    [self showSettingsMenu];
}

- (void)returnFromSettings {
    if(_settingsFromPause && _session && !_failed)[self showPauseMenu];
    else [self showProfileMenu];
}

- (void)showSettingsMenu {
    NSString* body=[NSString stringWithFormat:
        @"Enhanced original UI is shared by both modes. Modern adds supersampling and soft contact shadows; power and thermal limits reduce render resolution safely.\n\n%@",
        _graphicsModern?@"MODERN":@"CLASSIC"];
    [self showPanel:NativePanel::settings title:[self originalText:468] body:body token:++_localPanelToken];
    [self addCampaignButton:_graphicsModern?@"Graphics: Modern":@"Graphics: Classic" action:@selector(toggleGraphics)];
    [self addCampaignButton:[self originalText:[_audio musicEnabled]?495:1222] action:@selector(toggleMusic)];
    [self addCampaignButton:[self originalText:[_audio soundEnabled]?855:923] action:@selector(toggleSound)];
    [self addCampaignButton:[NSString stringWithFormat:@"Camera: %.2fx",_touchCameraSpeed]
        action:@selector(cycleTouchCameraSpeed)];
    [self addCampaignButton:@"Back" action:@selector(returnFromSettings)];
}

// The original iPhone text bank provides four real HELP topics (overview,
// controls, modes and tips). Retain their exact source text rather than a
// substitute instructions paragraph. This is native dispatch, not the old
// ARM32 UI callback table (which still needs independent parity verification).
- (void)showHelpMenu {
    _informationReturnPanel=_panelPurpose;
    [self showHelpIndex];
}
- (void)showHelpIndex {
    [self showPanel:NativePanel::help title:[self originalText:754] body:@""
        token:++_localPanelToken];
    const std::array<std::uint16_t,4> tabs{{630,1181,983,1200}};
    for(std::size_t i=0;i<tabs.size();++i) {
        UIButton* choice=[self addCampaignButton:[self originalText:tabs[i]]
            action:@selector(openOriginalHelpTopic:)];
        choice.tag=static_cast<NSInteger>(i);
    }
    [self addCampaignButton:@"Back" action:@selector(backFromInformation)];
}
- (void)openOriginalHelpTopic:(UIButton*)choice {
    if(_panelPurpose!=NativePanel::help || !choice || choice.tag<0 || choice.tag>=4)return;
    const std::array<std::uint16_t,4> titles{{630,1181,983,1200}};
    const std::array<std::uint16_t,4> bodies{{861,458,788,393}};
    const std::size_t i=static_cast<std::size_t>(choice.tag);
    [self showPanel:NativePanel::help_topic title:[self originalText:titles[i]]
        body:[self originalText:bodies[i]] token:++_localPanelToken];
    [self addCampaignButton:@"Back" action:@selector(showHelpIndex)];
}

- (void)showCreditsMenu {
    _informationReturnPanel=_panelPurpose;
    [self showPanel:NativePanel::credits title:@"CREDITS"
        body:@"TRANSFORMERS G1: AWAKENING\n\nFaithful native ARM64 iOS port by iammrmikeman. Original game models, art, sound and campaign data are retained. Unofficial fan project. Transformers and the original game remain the property of their respective rights holders. No old online services, Glu blog or feedback links. The app starts directly in the G1 menu."
        token:++_localPanelToken];
    [self addCampaignButton:@"Back" action:@selector(backFromInformation)];
}

- (void)backFromInformation {
    const auto destination=_informationReturnPanel;
    _informationReturnPanel=NativePanel::none;
    switch(destination) {
    case NativePanel::settings:[self showSettingsMenu];break;
    case NativePanel::pause:[self showPauseMenu];break;
    case NativePanel::mode_select:[self showModesMenu];break;
    case NativePanel::mission_select:[self showMissionPicker];break;
    case NativePanel::arena:[self chooseArenaFaction:_pendingArenaIndex];break;
    case NativePanel::arena_opponent:[self returnToArenaFaction];break;
    default:[self showProfileMenu];break;
    }
}

- (void)cycleTouchCameraSpeed {
    if(_panelPurpose!=NativePanel::settings)return;
    const float next=_touchCameraSpeed<0.875F ? 1.0F :
        _touchCameraSpeed<1.125F ? 1.25F :
        _touchCameraSpeed<1.375F ? 1.5F : 0.75F;
    _touchCameraSpeed=next;
    [NSUserDefaults.standardUserDefaults setFloat:next forKey:@"G1TouchCameraSpeed"];
    [self showSettingsMenu];
}

- (void)toggleGraphics {
    const bool next=!_graphicsModern;
    if(next) {
        try {
            [self primeModernTextures];
        } catch(const std::exception& e) {
            [self showPanel:NativePanel::settings title:@"Graphics could not be changed"
                body:native_text(e.what()) token:++_localPanelToken];
            [self addCampaignButton:@"Back" action:@selector(showSettingsMenu)];
            return;
        }
    }
    _graphicsModern=next;
    _camera.set_modern(next);
    if(!next){_tacticalModels.clear();_tacticalGpu.clear();_tacticalHitBounds.clear();}
    [NSUserDefaults.standardUserDefaults setBool:next forKey:@"G1GraphicsModern"];
    ++_contentGeneration;
    [self showSettingsMenu];
}

- (void)toggleDiagnostics {
    _showDiagnostics=!_showDiagnostics;
    [NSUserDefaults.standardUserDefaults setBool:_showDiagnostics forKey:@"G1DiagnosticsEnabled"];
    [self updateDiagnosticsVisibility];
    [self showSettingsMenu];
}

- (void)toggleMusic {
    const BOOL enabled=![_audio musicEnabled];
    [NSUserDefaults.standardUserDefaults setBool:enabled forKey:@"G1MusicEnabled"];
    [_audio setMusicEnabled:enabled soundEnabled:[_audio soundEnabled]];
    if(enabled && _settingsFromPause && _lastMapMusic)
        [self dispatchAudio:*_lastMapMusic];
    [self showSettingsMenu];
}
- (void)toggleSound {
    const BOOL enabled=![_audio soundEnabled];
    [NSUserDefaults.standardUserDefaults setBool:enabled forKey:@"G1SoundEnabled"];
    [_audio setMusicEnabled:[_audio musicEnabled] soundEnabled:enabled];
    [self showSettingsMenu];
}

- (void)installSession:(std::unique_ptr<g1::GameSession>)session {
    if (!session) throw g1::FormatError("Cannot install an empty native session");
    // A failed resource/GPU allocation leaves the menu retryable; it must not
    // let a draw callback dereference the partially replaced scene.
    _loaded=false;_metalView.paused=YES;
    // Numeric dialogue indices, battle tokens and per-unit generations can
    // repeat in a new session. Invalidate every old drawable callback before
    // replacing it, and clear UIKit caches before showing the new requests.
    ++_sceneRevision; ++_sessionInstalls;
    _presentationEpoch.invalidate();
    _session = std::move(session);
    _title.text=[self originalText:715];
    _scene.reset(); _cameraPan.reset();
    _preparedWorldToken.reset();_worldOverlaySignature.clear();_worldOverlays.clear();
    _animatedTerrain.reset();_decorationTerrain.reset();
    _terrainSpriteDraws.clear();
    _renderedTerrainGeneration=0;_renderedTerrainWorld=0;
    _animations.clear(); _animationIds.clear(); _renderedActive.clear();
    _playbackSpawnGenerations.clear(); _playbackTransformGenerations.clear();
    _pendingTransformCompletions.clear();
    _spriteVertices.clear(); _spriteMeshes.clear(); _unitModels.clear();
    _tacticalModels.clear();_tacticalGpu.clear();_tacticalErrors.clear();_tacticalHitBounds.clear();
    _inputEdges = 0; _inputCommand = std::numeric_limits<std::size_t>::max();
    _dialogueCommand = std::numeric_limits<std::size_t>::max();
    _lastMissionMode = 255; _lastSelection.reset();
    _pendingCompletionRequests.clear(); _completedBattles.clear();
    _battlePresentation.reset(); _battleModels.clear(); _battlePresentedToken=0;
    _supportPresentation.reset(); _supportUnitDraws.clear(); _supportCursor.reset();
    _supportSavedCursor.reset(); _supportDisplay.invalidate();
    _supportSavedTitle=nil;
    _supportTutorialRetryRequired=false;
    _presentedMessageIds.clear();
    _notice = nil; _noticeUntil = 0;
    _checkpointCommand=std::numeric_limits<std::size_t>::max();
    _checkpointRequested=true;_checkpointRetryRequired=false;_pendingMessageIds.clear();
    _campaignRetryRequired=false;
    _healMode=false;_healTargets.clear();_healRange.reset();
    _productionMode.reset();_productionType.reset();_productionPlacement.reset();_returnCursor.reset();
    _productionRange.reset();_typeListOpen=false;_controlQuads.clear();
    [_dialogueCanvas resetPresentation]; [_battleCanvas resetPresentation];
    _dialoguePanel.hidden = YES; _battlePanel.hidden = YES;
    _techSpecsPanel.hidden=YES;_techSpecsHotspot.hidden=YES;
    _techSpecsRoster.clear();
    [_gameHudFrame clearStatus];_dialoguePortrait.image=nil;
    _clockStarted = false; _simulationClock.reset(); _frameStats.times.clear();
    [self rebuildScene:YES];
    if (_session->world().camera) {
        _camera.center_x = (_session->world().camera->x+0.5F)*0.32F;
        _camera.center_z = (_session->world().camera->y+0.5F)*0.32F;
    }
    _loaded = true;
    _failed=false;_failure=nil;
}


- (void)primeModernTextures {
    // This updates only derived host textures. The original archive,
    // gameplay map, sprite geometry and shared RNG remain untouched.
    if(!_renderer || !_scene || !_archive)return;
    if(!_modernTextures)_modernTextures=[NSMutableDictionary new];
    const auto cache=[&](std::uint32_t id,const g1::RgbaImage& image) {
        if(image.width!=512 || image.height!=512)
            throw g1::FormatError("Modern sprite source image differs from verified 512x512");
        if(!_modernTextures[@(id)])
            _modernTextures[@(id)]=_renderer->texture(g1::clean_sprite_or_source(image));
    };
    for(const auto& image:_scene->textures) {
        if(image.first==_scene->terrain.atlas_resource_id && !_modernTextures[@(image.first)])
            _modernTextures[@(image.first)]=_renderer->texture(g1::clean_terrain_or_source(image.second));
        const bool sprite=std::any_of(_scene->sprites.begin(),_scene->sprites.end(),[&](const auto& item) {
            return item.resource.image_id==image.first;
        });
        if(sprite)cache(image.first,image.second);
    }
    for(const auto& decoration:_terrainSpriteDraws) {
        const auto& resource=decoration.resource;
        if(_modernTextures[@(resource.image_id)])continue;
        const auto matches=_archive->find(resource.image_id,resource.resource_key);
        if(matches.size()!=1)
            throw g1::FormatError("Modern terrain decoration source is missing or ambiguous");
        cache(resource.image_id,g1::decode_png(_archive->read(matches.front())));
    }
}
- (id<MTLTexture>)spriteTexture:(std::uint32_t)resource {
    if(_graphicsModern && _modernTextures[@(resource)])return _modernTextures[@(resource)];
    return _textures[@(resource)];
}

- (void)rebuildScene:(BOOL)resetPlayback {
    const auto& world = _session->world();
    const bool mapChanged = !_scene || !_scene->matches_map(world);
    auto next = std::make_unique<g1::MissionScene>(*_archive,_terrainTableBytes,world);
    auto terrain = mapChanged ? _renderer->mesh(g1::terrain_vertices(next->terrain)) : _terrain;
    auto animated=mapChanged ? (next->terrain.animated_surface ?
        _renderer->mesh(g1::terrain_vertices(next->terrain.animated_surface->quads,next->terrain.position_scale)) :
        std::shared_ptr<g1::MetalMesh>{}) : _animatedTerrain;
    auto controls=_controlQuads;
    if(mapChanged) {
        controls.clear();
        for(std::uint16_t y=0;y<world.grid.height;++y)for(std::uint16_t x=0;x<world.grid.width;++x) {
            const auto id=world.grid.tile_at(x,y);
            if(id>=32)throw g1::FormatError("Native input grid contains an unsupported cell");
            const auto height=std::int16_t(_terrainTables.at(world.style).at(id).height_steps*32);
            const auto left=std::int16_t(x*32),right=std::int16_t((x+1)*32);
            const auto top=std::int16_t(y*32),bottom=std::int16_t((y+1)*32);
            g1::TerrainQuad quad{};quad.tile_x=x;quad.tile_y=y;quad.face=g1::TerrainFace::top;
            quad.positions={{{{left,height,top}},{{right,height,top}},{{left,height,bottom}},{{right,height,bottom}}}};
            controls.push_back(quad);
        }
    }
    if (!_textures) _textures = [NSMutableDictionary new];
    if (!_modernTextures) _modernTextures = [NSMutableDictionary new];
    for (const auto& image : next->textures) {
        if (!_textures[@(image.first)]) _textures[@(image.first)] = _renderer->texture(image.second);
        if(_graphicsModern && !_modernTextures[@(image.first)]) {
            // Persisted Modern mode must load the same terrain derivative at
            // first launch/map change as the live graphics-toggle path.
            if(image.first==next->terrain.atlas_resource_id) {
                _modernTextures[@(image.first)]=_renderer->texture(g1::clean_terrain_or_source(image.second));
                continue;
            }
            const bool sprite=std::any_of(next->sprites.begin(),next->sprites.end(),[&](const auto& item) {
                return item.resource.image_id==image.first;
            });
            if(sprite && image.second.width==512 && image.second.height==512)
                _modernTextures[@(image.first)]=_renderer->texture(g1::clean_sprite_or_source(image.second));
        }
    }
    if (!_white) _white = _renderer->texture({1,1,{255,255,255,255}});

    std::map<std::uint8_t,std::size_t> oldByType;
    if (_scene) for (std::size_t i=0;i<_scene->sprites.size();++i)
        oldByType.emplace(_scene->world.units.at(_scene->sprites[i].unit_index).type,i);
    decltype(_spriteVertices) vertices;
    decltype(_spriteMeshes) meshes;
    decltype(_animations) animations;
    decltype(_animationIds) animationIds;
    decltype(_unitModels) models;
    decltype(_renderedActive) active;
    decltype(_playbackSpawnGenerations) spawnGenerations, transformGenerations;
    decltype(_pendingTransformCompletions) completions;
    std::vector<std::size_t> resetIndices;
    for (const auto& sprite : next->sprites) {
        const auto index = sprite.unit_index;
        const auto type = world.units.at(index).type;
        const auto found = oldByType.find(type);
        const auto& unit = _session->units().at(index);
        const bool reuse = !resetPlayback && !mapChanged && found != oldByType.end();
        if (reuse) {
            const auto old = found->second;
            vertices.push_back(_spriteVertices.at(old)); meshes.push_back(_spriteMeshes.at(old));
            animations.push_back(_animations.at(old)); animationIds.push_back(_animationIds.at(old));
            active.push_back(_renderedActive.at(old)); models.push_back(_unitModels.at(old));
            spawnGenerations.push_back(_playbackSpawnGenerations.at(old));
            transformGenerations.push_back(_playbackTransformGenerations.at(old));
            completions.push_back(_pendingTransformCompletions.at(old));
        } else {
            std::vector<std::vector<g1::SceneVertex>> frames;
            std::vector<std::shared_ptr<g1::MetalMesh>> frameMeshes;
            for (std::size_t frame=0;frame<sprite.descriptor.frames().size();++frame) {
                frames.push_back(g1::sprite_vertices(sprite.descriptor,frame));
                frameMeshes.push_back(_renderer->mesh(frames.back()));
            }
            vertices.push_back(std::move(frames)); meshes.push_back(std::move(frameMeshes));
            animations.push_back(std::nullopt); animationIds.push_back(-1);
            active.push_back(false); models.push_back(g1::identity_matrix());
            spawnGenerations.push_back(0); transformGenerations.push_back(0);
            completions.push_back(std::nullopt);
        }
        if (unit.active && (!animations.back() || !active.back() ||
            spawnGenerations.back() != _session->unit_spawn_generation(index))) {
            if (unit.form > 1 || unit.facing > 3) throw g1::FormatError("New unit has unsupported sprite form/facing");
            const auto value = _animationTable.at(type).at(unit.form*4+unit.facing);
            const int animation = value < 128 ? int(value) : int(value)-256;
            if (animation < 0 || static_cast<std::size_t>(animation) >= sprite.descriptor.animations().size())
                throw g1::FormatError("New unit's initial animation is not present in its descriptor");
            animations.back().emplace(sprite.descriptor.animations()[animation],-1,false);
            animationIds.back() = animation;
            models.back() = unit_model(world,unit);
            active.back() = true;
            spawnGenerations.back() = _session->unit_spawn_generation(index);
            transformGenerations.back() = 0;
            completions.back().reset();
            resetIndices.push_back(index);
            ++_spawnPlaybackResets;
        } else if (!unit.active) {
            animations.back().reset(); animationIds.back() = -1; active.back() = false;
            transformGenerations.back() = 0; completions.back().reset();
        }
    }
    _scene = std::move(next); _terrain = std::move(terrain);_animatedTerrain=std::move(animated);
    _controlQuads=std::move(controls);
    _spriteVertices = std::move(vertices); _spriteMeshes = std::move(meshes);
    _animations = std::move(animations); _animationIds = std::move(animationIds);
    _unitModels = std::move(models); _renderedActive = std::move(active);
    _playbackSpawnGenerations = std::move(spawnGenerations);
    _playbackTransformGenerations = std::move(transformGenerations);
    _pendingTransformCompletions = std::move(completions);
    if (mapChanged || resetPlayback) _previousUnits = _session->units();
    else while (_previousUnits.size() < _session->units().size())
        _previousUnits.push_back(_session->units()[_previousUnits.size()]);
    for (const auto index : resetIndices) _previousUnits.at(index) = _session->units().at(index);
    _range.reset(); _cursor.reset(); _attackRange.reset();_healRange.reset(); _rangeDirty = true;
    _moveMode = false; _attackMode = false;_healMode=false; _pendingDestination.reset();
    _previewRange.reset(); _attackTargets.clear();_healTargets.clear(); _lastSelection.reset();
    _productionMode.reset();_productionType.reset();_productionPlacement.reset();_productionRange.reset();
    _returnCursor.reset();_typeListOpen=false;
    _inputEdges = 0;
    _dialogueCommand = std::numeric_limits<std::size_t>::max();
    if (mapChanged) {
        _cameraPan.reset(); _camera.set_modern(_graphicsModern); _camera.fit(*_scene,_metalView.bounds.size);
        _decorationTerrain.reset();_renderedTerrainGeneration=0;_renderedTerrainWorld=0;
        _terrainSpriteDraws.clear();
        _worldOverlaySignature.clear();
    }
    ++_sceneRevision; ++_sceneRebuilds; ++_contentGeneration;
}

- (void)synchronizeScene {
    if (!_scene || !_scene->matches_map(_session->world()) ||
        !_scene->matches_roster(_session->world().units)) [self rebuildScene:NO];
}

- (void)refreshWorldOverlays {
    const auto& world=_session->world();
    const auto& network=_session->captures();
    if(world.sites.size()!=network.sites.size() || world.sites.size()!=network.item_links.size())
        throw g1::FormatError("Visible site ownership differs from the current capture network");
    std::vector<std::int32_t> signature{static_cast<std::int32_t>(world.grid_resource_id),world.style,
        static_cast<std::int32_t>(world.sites.size())};
    for(std::size_t i=0;i<world.sites.size();++i) {
        const auto& site=world.sites[i];const auto& state=network.sites[i];
        signature.insert(signature.end(),{site.position.x,site.position.y,state.state,
            static_cast<std::int32_t>(site.coordinate_groups.size())});
        if(site.coordinate_groups.size()!=state.item_states.size() ||
           site.coordinate_groups.size()!=network.item_links[i].size())
            throw g1::FormatError("Visible site group shape is inconsistent");
        for(std::size_t g=0;g<site.coordinate_groups.size();++g) {
            const auto& group=site.coordinate_groups[g];
            if(group.size()!=state.item_states[g].size() || group.size()!=network.item_links[i][g].size())
                throw g1::FormatError("Visible capture point shape is inconsistent");
            signature.push_back(static_cast<std::int32_t>(group.size()));
            for(std::size_t j=0;j<group.size();++j)
                signature.insert(signature.end(),{group[j].x,group[j].y,state.item_states[g][j],network.item_links[i][g][j]});
        }
    }
    signature.push_back(static_cast<std::int32_t>(world.site_points.size()));
    for(const auto& point:world.site_points)
        signature.insert(signature.end(),{point.point.x,point.point.y,point.point.state});
    for(const auto& marker:{world.marker_254,world.marker_25c})
        signature.insert(signature.end(),{marker[0],marker[1]});
    if(signature==_worldOverlaySignature)return;
    std::vector<NativeWorldOverlay> overlays;
    const auto color=[](int owner) -> std::array<float,4> {
        if(owner==0)return {{0.18F,0.70F,1.0F,0.88F}};
        if(owner==1)return {{1.0F,0.27F,0.30F,0.88F}};
        if(owner==-1)return {{0.85F,0.85F,0.85F,0.84F}};
        throw g1::FormatError("Unknown displayed site owner");
    };
    const auto add=[&](int x,int y,int shape,std::array<float,4> tint,bool linked) {
        if(x<0 || y<0 || x>=world.grid.width || y>=world.grid.height)
            throw g1::FormatError("Visible world marker is outside its map");
        const auto tile=world.grid.tile_at(static_cast<std::uint16_t>(x),static_cast<std::uint16_t>(y));
        if(tile>=32)throw g1::FormatError("Visible marker uses an unsupported terrain cell");
        const float px=(float(x)+0.5F)*0.32F,pz=(float(y)+0.5F)*0.32F;
        const float height=float(_terrainTables.at(world.style).at(tile).height_steps)*0.32F+0.012F;
        std::vector<g1::SceneVertex> vertices;
        const auto line=[&](float ax,float az,float bx,float bz,float width) {
            const float length=std::hypot(bx-ax,bz-az);
            if(length<=0)return;
            const float dx=(bz-az)*width/(2*length),dz=(ax-bx)*width/(2*length);
            const std::array<std::array<float,2>,4> p{{{{ax-dx,az-dz}},{{bx-dx,bz-dz}},
                {{ax+dx,az+dz}},{{bx+dx,bz+dz}}}};
            for(const auto index:g1::sprite_triangle_indices)
                vertices.push_back({{{px+p[index][0],height,pz+p[index][1],1}},{{0,0}}, {}});
        };
        const float r=shape==0 ? 0.135F : shape==1 ? 0.105F : 0.145F;
        if(shape==0) {
            line(-r,-r,r,-r,0.025F);line(r,-r,r,r,0.025F);
            line(r,r,-r,r,0.025F);line(-r,r,-r,-r,0.025F);
        } else if(shape==1 || shape==3) {
            line(0,-r,r,0,0.025F);line(r,0,0,r,0.025F);
            line(0,r,-r,0,0.025F);line(-r,0,0,-r,0.025F);
        } else {
            line(-r,0,r,0,0.025F);line(0,-r,0,r,0.025F);
            if(shape==4){line(-r,-r,r,r,0.016F);line(-r,r,r,-r,0.016F);}
        }
        if(linked){line(-0.035F,0,0.035F,0,0.025F);line(0,-0.035F,0,0.035F,0.025F);}
        overlays.push_back({_renderer->mesh(vertices),tint});
    };
    for(std::size_t i=0;i<world.sites.size();++i) {
        const auto& site=world.sites[i];const auto& state=network.sites[i];
        add(site.position.x,site.position.y,0,color(state.state),false);
        for(std::size_t g=0;g<site.coordinate_groups.size();++g)
            for(std::size_t j=0;j<site.coordinate_groups[g].size();++j) {
                const auto p=site.coordinate_groups[g][j];
                add(p.x,p.y,1,color(state.item_states[g][j]),network.item_links[i][g][j]!=0);
            }
    }
    for(const auto& point:world.site_points)
        if(point.point.state!=0)add(point.point.x,point.point.y,4,{{0.75F,0.4F,1,0.85F}},false);
    if(world.marker_254[0]>=0 && world.marker_254[1]>=0)
        add(world.marker_254[0],world.marker_254[1],2,{{1,0.83F,0.16F,0.9F}},false);
    if(world.marker_25c[0]>=0 && world.marker_25c[1]>=0)
        add(world.marker_25c[0],world.marker_25c[1],3,{{0.15F,1,0.85F,0.9F}},false);
    _worldOverlays=std::move(overlays);_worldOverlaySignature=std::move(signature);
    ++_contentGeneration;
}

- (void)prepareWorldPresentation {
    if(!_session->world_presentation())return;
    const auto& presentation=*_session->world_presentation();
    if(_preparedWorldToken && *_preparedWorldToken==presentation.token)return;
    [self synchronizeScene];[self refreshWorldOverlays];
    for(const auto& plan:presentation.operations) {
        if(const auto map=std::get_if<g1::WorldMapInstall>(&plan.data)) {
            if(map->music_request){_lastMapMusic=map->music_request;[self dispatchAudio:*map->music_request];}
            if(map->stop_music_if_config_a4_disabled && ![_audio musicEnabled])
                [self dispatchAudio:g1::MissionAudioRequest{3,0}];
        } else if(const auto phase=std::get_if<g1::WorldPhaseCommand>(&plan.data)) {
            if(phase->title_text_id)_title.text=[self originalText:*phase->title_text_id];
        }
        // Core already applied these plans. Geometry/ownership/markers come
        // from the actual current session, never from replaying a mutation.
    }
    _preparedWorldToken=presentation.token;
    ++_sceneRevision;++_contentGeneration;
}

- (void)refreshTerrainDecorations {
    if(_renderedTerrainGeneration==_session->terrain_generation() &&
       _renderedTerrainWorld==_session->world_generation())return;
    const auto* state=_session->terrain_state();
    const auto* layout=_session->terrain_layout();
    if(!_scene->terrain.animated_surface) {
        if(state && !state->slots.empty())throw g1::FormatError("Renderable terrain slots lack their material");
        _decorationTerrain.reset();
    } else {
        if(!state || !layout || !_session->terrain_tables())
            throw g1::FormatError("Animated terrain lacks its session-owned runtime");
        const auto quads=g1::terrain_decoration_quads(*layout,*state,_session->world().grid,*_session->terrain_tables());
        _decorationTerrain=_renderer->mesh(g1::terrain_vertices(quads,_scene->terrain.position_scale));
    }
    std::vector<g1::TerrainSpriteDecorationDraw> sprites;
    if(const auto* spriteState=_session->terrain_sprite_state()) {
        const auto* spriteLayout=_session->terrain_sprite_layout();
        const auto* bank=_session->terrain_sprite_animations();
        if(!spriteLayout || !bank)throw g1::FormatError("Terrain sprite state has no decoded layout or animations");
        sprites=g1::terrain_sprite_decoration_draws(*spriteLayout,*bank,*spriteState);
        const auto read=[&](std::uint32_t id,std::uint32_t key) {
            const auto found=_archive->find(id,key);
            if(found.size()!=1)throw g1::FormatError("Terrain sprite resource is missing or ambiguous");
            return _archive->read(found.front());
        };
        for(const auto& sprite:sprites) {
            const auto& resource=sprite.resource;
            if(!_textures[@(resource.image_id)]) {
                const auto image=g1::decode_png(read(resource.image_id,resource.resource_key));
                if(image.width!=512 || image.height!=512)
                    throw g1::FormatError("Terrain sprite atlas dimensions differ from original512x512");
                _textures[@(resource.image_id)]=_renderer->texture(image);
            }
            if(_graphicsModern && !_modernTextures[@(resource.image_id)]) {
                const auto image=g1::decode_png(read(resource.image_id,resource.resource_key));
                if(image.width!=512 || image.height!=512)
                    throw g1::FormatError("Modern decoration source is not a 512x512 atlas");
                _modernTextures[@(resource.image_id)]=_renderer->texture(g1::clean_sprite_or_source(image));
            }
            if(!_terrainSpriteMeshes.count(resource.descriptor_id)) {
                const g1::AtlasDescriptor descriptor(read(resource.descriptor_id,resource.resource_key));
                std::vector<std::shared_ptr<g1::MetalMesh>> frames;
                for(std::size_t i=0;i<descriptor.frames().size();++i)
                    frames.push_back(_renderer->mesh(g1::sprite_vertices(descriptor,i)));
                _terrainSpriteMeshes.emplace(resource.descriptor_id,std::move(frames));
            }
            if(sprite.frame_index>=_terrainSpriteMeshes.at(resource.descriptor_id).size())
                throw g1::FormatError("Terrain sprite frame is outside its actual descriptor");
        }
    }
    _terrainSpriteDraws=std::move(sprites);
    _renderedTerrainGeneration=_session->terrain_generation();_renderedTerrainWorld=_session->world_generation();
    ++_contentGeneration;
}

- (void)didReceiveMemoryWarning {
    [super didReceiveMemoryWarning];
    _renderMemoryPressure=true;
    if(_supersampling)_supersampling->release_targets();
    // Keep current source assets and saves intact. Supersampling stays off
    // for this process after memory pressure instead of repeatedly reallocating.
}
- (void)dealloc {
    [_menuMusicTimer invalidate];
    [_openingPlayer pause];
    [[NSNotificationCenter defaultCenter] removeObserver:self];
}

- (void)fail:(NSString*)message {
    _failed = true;
    _failure = [message copy];
    _metalView.paused = YES;
    _title.text = @"G1 Awakening — test stopped";
    _title.hidden=NO; _hint.hidden=NO;
    _hint.text = message;
    _hint.textColor = [UIColor colorWithRed:1 green:0.6 blue:0.55 alpha:1];
    _commands.hidden = YES;
    _dialoguePanel.hidden = YES;
    _battlePanel.hidden = YES;
    _techSpecsPanel.hidden=YES;_techSpecsHotspot.hidden=YES;
    _campaignPanel.hidden=YES;_panelPurpose=NativePanel::none;
    _typeListOpen=false;
    if([self.presentedViewController isKindOfClass:UIAlertController.class])
        [self dismissViewControllerAnimated:NO completion:nil];
}

- (void)inactive:(NSNotification*)notification {
    (void)notification;
    if(_openingVisible)[_openingPlayer pause];
    _checkpointRequested=true;[self saveCheckpoint];
    _metalView.paused = YES;
    _clockStarted = false;
    _simulationClock.reset();
    _supportDisplay.invalidate();
    _presentationEpoch.invalidate();
    _inputEdges = 0;
    if (_session) _previousUnits = _session->units();
    _frameStats.times.clear();
    [_audio setForegroundActive:NO];
}

- (void)active:(NSNotification*)notification {
    (void)notification;
    if(_openingVisible)[_openingPlayer play];
    _presentationEpoch.invalidate();
    _clockStarted = false;
    _simulationClock.reset();
    if (_session) _previousUnits = _session->units();
    if (_loaded && !_failed && _panelPurpose!=NativePanel::tech_specs)
        _metalView.paused = NO;
    [_audio setForegroundActive:YES];
}

- (void)resetView {
    if (!_scene || _panelPurpose!=NativePanel::none) return;
    if (_session && (_session->mission().gate == g1::MissionGate::camera ||
        _session->focus_request() || _session->battle())) return;
    _camera.set_modern(_graphicsModern); _camera.fit(*_scene,_metalView.bounds.size);
    ++_contentGeneration;
}

- (CGRect)originalGameplayViewport {
    return g1::adaptive_ui::cg(g1::adaptive_ui::layout(self.view).source);
}

- (void)layoutOriginalGameplayUI {
    using namespace g1::adaptive;
    const auto l=g1::adaptive_ui::layout(self.view);
    if(l.scale<=0)return;
    const CGFloat scale=l.scale,extra=l.virtual_width-480.0;
    const auto projected=[&](g1::adaptive::Rect r,Anchor h=Anchor::center,Anchor v=Anchor::center) {
        return g1::adaptive_ui::cg(l.place(r,h,v));
    };
    _gameHudFrame.frame=g1::adaptive_ui::cg(l.bottom(62));
    _techSpecsPanel.frame=self.view.bounds;
    _techSpecsCanvas.frame=CGRectMake(l.safe.x,l.source.y,l.safe.width,320*scale);
    _techSpecsModel.frame=CGRectMake(123*scale,36*scale,(232+extra)*scale,274*scale);
    _specsBackButton.frame=CGRectMake(0,0,62*scale,53*scale);
    _specsPreviousButton.frame=CGRectMake(0,65*scale,73*scale,83*scale);
    _specsNextButton.frame=CGRectMake((405+extra)*scale,65*scale,75*scale,83*scale);
    _specsRobotButton.frame=CGRectMake(75*scale,137*scale,48*scale,43*scale);
    _specsVehicleButton.frame=CGRectMake((357+extra)*scale,137*scale,48*scale,43*scale);
    _techSpecsHotspot.frame=projected({0,258,60,62},Anchor::start,Anchor::end);
    // Battle overlay spans the safe viewport: damage is above the combatants,
    // not incorrectly written into their health plates.
    _battlePanel.frame=g1::adaptive_ui::cg(l.safe);_battleCanvas.frame=_battlePanel.bounds;
    _dialoguePanel.frame=g1::adaptive_ui::cg(l.bottom(93));
    _dialogueBackdrop.frame=_dialoguePanel.bounds;
    _dialoguePortrait.frame=CGRectMake(81*scale,17*scale,78*scale,69*scale);
    _dialogueHeading.frame=CGRectMake(174*scale,17*scale,(157+extra)*scale,14*scale);
    _dialogueHeading.font=[UIFont boldSystemFontOfSize:10*scale];
    _dialogueCanvas.frame=CGRectMake(174*scale,31*scale,(151+extra)*scale,54*scale);
    [_dialogueCanvas setPresentationScale:scale];
    _menuButton.frame=projected({0,0,60,49},Anchor::start,Anchor::start);
    _commands.frame=self.view.bounds;
    const auto place=[&](UIButton* button,g1::adaptive::Rect r,Anchor h){button.frame=projected(r,h);};
    using namespace g1::original_presentation;
    place(_healButton,source_left_actions[0],Anchor::start);
    place(_attackButton,source_left_actions[1],Anchor::start);
    place(_undoButton,source_left_actions[2],Anchor::start);
    place(_buildButton,source_left_actions[3],Anchor::start);
    place(_moveButton,source_right_actions[0],Anchor::end);
    place(_captureButton,source_right_actions[1],Anchor::end);
    place(_transformButton,source_right_actions[2],Anchor::end);
    place(_upgradeButton,source_right_actions[3],Anchor::end);
    place(_deployButton,source_left_actions[3],Anchor::start);
    _endTurnButton.frame=projected(end_turn,Anchor::center,Anchor::start);
    _resetButton.frame=projected({64,3,85,27},Anchor::start,Anchor::start);
    _reportButton.frame=projected({340,3,85,27},Anchor::end,Anchor::start);
}

- (void)setSourceTopLeftDialogueMode:(BOOL)dialogue {
    if(_menuIsSkip==dialogue)return;
    _menuIsSkip=dialogue;
    [_menuButton removeTarget:self action:@selector(openMenu) forControlEvents:UIControlEventTouchUpInside];
    [_menuButton removeTarget:self action:@selector(dialogueSkip) forControlEvents:UIControlEventTouchUpInside];
    [_menuButton addTarget:self action:dialogue ? @selector(dialogueSkip) : @selector(openMenu)
        forControlEvents:UIControlEventTouchUpInside];
    UIImage* icon=dialogue?_skipDialogueIcon:_pauseGameIcon;
    if(icon)[_menuButton setImage:icon forState:UIControlStateNormal];
    _menuButton.accessibilityLabel=dialogue?@"Skip dialogue":@"Pause menu";
}

- (void)dialogueTap:(UITapGestureRecognizer*)gesture {
    if(gesture.state==UIGestureRecognizerStateEnded)[self dialogueNext];
}

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    if(_openingLayer)_openingLayer.frame=_openingView.bounds;
    [self updateCampaignLayout];
    [self layoutOriginalGameplayUI];
    UIScreen* screen = self.view.window.screen ?: UIScreen.mainScreen;
    // Allocate the render target at the attached display's native pixel scale,
    // not the old game's 480x320 framebuffer size. UIKit controls stay in points.
    if (_metalView.contentScaleFactor != screen.nativeScale)
        _metalView.contentScaleFactor = screen.nativeScale;
    const CGSize nativeSize = CGSizeMake(std::round(_metalView.bounds.size.width * screen.nativeScale),
                                          std::round(_metalView.bounds.size.height * screen.nativeScale));
    if (!CGSizeEqualToSize(_metalView.drawableSize,nativeSize)) _metalView.drawableSize = nativeSize;
    _metalView.preferredFramesPerSecond = MIN(60,screen.maximumFramesPerSecond);
}

- (void)mtkView:(MTKView*)view drawableSizeWillChange:(CGSize)size {
    (void)view;
    if (_scene && size.height > 0) {
        _camera.aspect = static_cast<float>(size.width / size.height);
        ++_contentGeneration;
    }
}

- (void)pan:(UIPanGestureRecognizer*)gesture {
    if (!_loaded || _typeListOpen || _panelPurpose!=NativePanel::none || _session->world_presentation() ||
        _metalView.bounds.size.height <= 0 || _session->mission().interaction_mode != 0 ||
        _session->focus_request() || _session->battle()) return;
    const CGPoint delta = [gesture translationInView:_metalView];
    const float scale = _camera.span / static_cast<float>(_metalView.bounds.size.height);
    _camera.center_x -= static_cast<float>(delta.x) * scale * _touchCameraSpeed;
    _camera.center_z += static_cast<float>(delta.y) * scale * _touchCameraSpeed / _camera.up_z;
    ++_contentGeneration;
    [gesture setTranslation:CGPointZero inView:_metalView];
}

- (void)pinch:(UIPinchGestureRecognizer*)gesture {
    if (!_loaded || _typeListOpen || _panelPurpose!=NativePanel::none || _session->world_presentation() ||
        _session->mission().interaction_mode != 0 || _session->focus_request() || _session->battle()) return;
    if (std::isfinite(gesture.scale) && gesture.scale > 0)
        _camera.span = std::clamp(_camera.span /
                                  std::pow(static_cast<float>(gesture.scale),_touchCameraSpeed),
                                  _camera.base_span * 0.2F, _camera.base_span * 3.0F);
    gesture.scale = 1;
    ++_contentGeneration;
}

- (std::optional<std::size_t>)unitAtPoint:(CGPoint)point padding:(CGFloat)padding {
    const auto size=_metalView.bounds.size;
    std::optional<std::size_t> selected;
    double best=std::numeric_limits<double>::max();
    if(size.width<=0 || size.height<=0)return selected;
    for(std::size_t i=0;i<_animations.size();++i) {
        const auto index=_scene->sprites[i].unit_index;
        if(!_session->units().at(index).active || !_animations[i])continue;
        const auto matrix=g1::multiply_matrix(_camera.matrix(),_unitModels[i]);
        CGRect bounds=CGRectNull;
        for(const auto& vertex:_spriteVertices[i][_animations[i]->current_frame()]) {
            const auto p=transform(matrix,vertex.position);
            const double x=(p[0]/p[3]*0.5+0.5)*size.width,y=(0.5-p[1]/p[3]*0.5)*size.height;
            bounds=CGRectUnion(bounds,CGRectMake(x,y,0.1,0.1));
        }
        if(_graphicsModern) {
            const auto found=_tacticalHitBounds.find(i);
            if(found!=_tacticalHitBounds.end())bounds=found->second;
        }
        if(!CGRectIsNull(bounds) && CGRectContainsPoint(CGRectInset(bounds,-padding,-padding),point)) {
            const double distance=std::hypot(point.x-CGRectGetMidX(bounds),point.y-CGRectGetMidY(bounds));
            if(distance<best){best=distance;selected=i;}
        }
    }
    return selected;
}

- (void)tap:(UITapGestureRecognizer*)gesture {
    if (!_loaded || _failed || _typeListOpen || _panelPurpose!=NativePanel::none || _session->world_presentation()) return;
    try {
        if (_session->mission().interaction_mode != 0 || _session->moving() ||
            _session->focus_request() || _session->battle()) return;
        const auto point = [gesture locationInView:_metalView];
        const auto size = _metalView.bounds.size;
        if (size.width <= 0 || size.height <= 0) return;
        const auto hitUnit=[self unitAtPoint:point padding:_moveMode ? 0 : 14];
        std::optional<g1::WorldCoordinate> tile;
        float closest = std::numeric_limits<float>::max();
        const auto camera = _camera.matrix();
        for (const auto& quad : _controlQuads) {
            if (quad.face != g1::TerrainFace::top) continue;
            std::array<CGPoint,4> screen{};
            float depth = 0;
            for (std::size_t i=0;i<4;++i) {
                const auto& p = quad.positions[i];
                const auto clip = transform(camera,{{p[0]*_scene->terrain.position_scale,
                    p[1]*_scene->terrain.position_scale,p[2]*_scene->terrain.position_scale,1}});
                screen[i] = CGPointMake((clip[0]/clip[3]*0.5+0.5)*size.width,
                                          (0.5-clip[1]/clip[3]*0.5)*size.height);
                depth += clip[2]/clip[3];
            }
            if ((inside_triangle(point,screen[0],screen[1],screen[2]) ||
                 inside_triangle(point,screen[2],screen[1],screen[3])) && depth < closest) {
                closest = depth;
                tile = g1::WorldCoordinate{static_cast<std::uint8_t>(quad.tile_x),
                                          static_cast<std::uint8_t>(quad.tile_y)};
            }
        }
        if(_moveMode && hitUnit) {
            const auto& unit=_session->units().at(_scene->sprites[*hitUnit].unit_index);
            tile=g1::WorldCoordinate{unit.tile_x,unit.tile_y};
        }
        if(_productionMode && _productionType && tile && [self productionTileAllowed:*tile]) {
            if(_pendingDestination && _pendingDestination->x==tile->x && _pendingDestination->y==tile->y) {
                const auto result=_session->produce_selected(*_productionMode,*_productionType,*tile,
                    g1::ProductionConfirmation::direct_type_list);
                if(result) {
                    ++_productionActions;_checkpointRequested=true;
                    [self dispatchAudio:g1::MissionAudioRequest{result->audio_operation,result->audio_argument}];
                    [self synchronizeScene];
                    _returnCursor=result->return_cursor;
                    _notice=@"Unit activated.";_noticeUntil=CACurrentMediaTime()+3;
                } else {_notice=@"That activation is not available.";_noticeUntil=CACurrentMediaTime()+3;}
                _productionMode.reset();_productionType.reset();_productionPlacement.reset();_pendingDestination.reset();
            } else {_pendingDestination=tile;_notice=@"Tap the same tile again to activate.";_noticeUntil=CACurrentMediaTime()+3;}
            _rangeDirty=true;[self refreshControls];return;
        }
        if (_moveMode && tile && _previewRange &&
            _previewRange->at(tile->x,tile->y) >= 0 &&
            _previewRange->at(tile->x,tile->y) != g1::movement_reserved_cell) {
            if (_pendingDestination && _pendingDestination->x == tile->x && _pendingDestination->y == tile->y) {
                const auto result = _session->move_selected_to(*tile);
                if (result == g1::MoveRequestResult::started) {
                    ++_moveActions;
                    _checkpointRequested=true;
                    _moveMode = false;
                    _pendingDestination.reset();
                    _rangeDirty = true;
                    _notice = @"Moving…";
                } else {
                    _notice = @"That move is not available.";
                }
            } else {
                _pendingDestination = tile;
                _rangeDirty = true;
                _notice = @"Tap the same tile again to move.";
            }
            _noticeUntil = CACurrentMediaTime()+3;
            [self refreshControls];
            return;
        }
        const NSInteger selected=hitUnit ? static_cast<NSInteger>(*hitUnit) : -1;
        if (selected >= 0) {
            const auto index = _scene->sprites[static_cast<std::size_t>(selected)].unit_index;
            if (_healMode) {
                if(std::find(_healTargets.begin(),_healTargets.end(),index)!=_healTargets.end()) {
                    const auto result=_session->heal_selected(index);
                    if(result) {
                        if(result->outcome==g1::HealOutcome::healed) {
                            ++_healActions;_checkpointRequested=true;
                            _notice=[NSString stringWithFormat:@"HP %u · Energy %d",
                                unsigned(result->target.health),result->funds_after];
                            _noticeUntil=CACurrentMediaTime()+3;
                        }
                        if(result->audio_event_in_group5)
                            [self dispatchAudio:g1::MissionAudioRequest{5,*result->audio_event_in_group5}];
                        if(result->message_id)_pendingMessageIds.push_back(*result->message_id);
                    }
                    _healMode=false;_rangeDirty=true;
                } else {_notice=@"Select a highlighted friendly unit.";_noticeUntil=CACurrentMediaTime()+3;}
                [self refreshControls];return;
            }
            if (_attackMode) {
                if (std::find(_attackTargets.begin(),_attackTargets.end(),index) != _attackTargets.end() &&
                    _session->attack_selected(index)) {
                    ++_attackActions;
                    _checkpointRequested=true;
                    _attackMode = false;
                    _rangeDirty = true;
                    _notice = nil;
                } else {
                    _notice = @"Select a highlighted enemy.";
                    _noticeUntil = CACurrentMediaTime()+3;
                }
                [self refreshControls];
                return;
            }
            if (_session->select(index)) {
                _moveMode = false;
                _attackMode = false;
                _healMode = false;
                _productionMode.reset();_productionType.reset();_productionPlacement.reset();_returnCursor.reset();
                _pendingDestination.reset();
                _notice = nil;
                _rangeDirty = true;
            } else {
                _notice = @"Select one of your available units.";
                _noticeUntil = CACurrentMediaTime()+3;
            }
        } else if (!_moveMode && !_attackMode && !_healMode && !_productionMode) {
            _session->clear_selection();
            _rangeDirty = true;
        }
        [self refreshControls];
    } catch (const std::exception& error) {
        [self fail:[NSString stringWithUTF8String:error.what()]];
    }
}

- (void)refreshRange {
    if (!_rangeDirty) return;
    _rangeDirty = false;
    _range.reset(); _cursor.reset(); _attackRange.reset();_healRange.reset();_productionRange.reset();
    _previewRange = _session->selected_movement_range();
    _attackTargets = _session->selected_attack_targets();
    _healTargets = _session->selected_heal_targets();
    if (!_previewRange) {
        _moveMode = false;
        _pendingDestination.reset();
    }
    if (_attackTargets.empty()) _attackMode = false;
    if (_healTargets.empty()) _healMode=false;
    if (_moveMode && _previewRange) {
        g1::TerrainMesh selected;
        selected.position_scale = _scene->terrain.position_scale;
        g1::TerrainMesh cursor;
        cursor.position_scale = selected.position_scale;
        for (const auto& quad : _controlQuads)
            if (quad.face == g1::TerrainFace::top && _previewRange->at(quad.tile_x,quad.tile_y) >= 0 &&
                _previewRange->at(quad.tile_x,quad.tile_y) != g1::movement_reserved_cell) {
                selected.quads.push_back(quad);
                if (_pendingDestination && _pendingDestination->x == quad.tile_x &&
                    _pendingDestination->y == quad.tile_y) cursor.quads.push_back(quad);
            }
        auto vertices = g1::terrain_vertices(selected);
        for (auto& vertex : vertices) { vertex.position[1] += 0.003F; vertex.uv = {{0,0}}; }
        if (!vertices.empty()) _range = _renderer->mesh(vertices);
        vertices = g1::terrain_vertices(cursor);
        for (auto& vertex : vertices) { vertex.position[1] += 0.006F; vertex.uv = {{0,0}}; }
        if (!vertices.empty()) _cursor = _renderer->mesh(vertices);
    }
    if (_attackMode) {
        g1::TerrainMesh targets;
        targets.position_scale = _scene->terrain.position_scale;
        for (const auto& quad : _controlQuads) if (quad.face == g1::TerrainFace::top)
            for (const auto index : _attackTargets) {
                const auto& unit = _session->units().at(index);
                if (unit.tile_x == quad.tile_x && unit.tile_y == quad.tile_y) {
                    targets.quads.push_back(quad);
                    break;
                }
            }
        auto vertices = g1::terrain_vertices(targets);
        for (auto& vertex : vertices) { vertex.position[1] += 0.006F; vertex.uv = {{0,0}}; }
        if (!vertices.empty()) _attackRange = _renderer->mesh(vertices);
    }
    if(_healMode) {
        g1::TerrainMesh targets;targets.position_scale=_scene->terrain.position_scale;
        for(const auto& quad:_controlQuads)if(quad.face==g1::TerrainFace::top)
            for(const auto index:_healTargets) {
                const auto& unit=_session->units().at(index);
                if(unit.tile_x==quad.tile_x && unit.tile_y==quad.tile_y){targets.quads.push_back(quad);break;}
            }
        auto vertices=g1::terrain_vertices(targets);
        for(auto& vertex:vertices){vertex.position[1]+=0.006F;vertex.uv={{0,0}};}
        if(!vertices.empty())_healRange=_renderer->mesh(vertices);
    }
    if(_productionMode && _productionType) {
        _productionPlacement=_session->selected_production_placement(*_productionMode);
        g1::TerrainMesh targets,cursor;targets.position_scale=cursor.position_scale=_scene->terrain.position_scale;
        for(const auto& quad:_controlQuads)
            if([self productionTileAllowed:g1::WorldCoordinate{static_cast<std::uint8_t>(quad.tile_x),static_cast<std::uint8_t>(quad.tile_y)}]) {
                targets.quads.push_back(quad);
                if(_pendingDestination && _pendingDestination->x==quad.tile_x && _pendingDestination->y==quad.tile_y)
                    cursor.quads.push_back(quad);
            }
        auto vertices=g1::terrain_vertices(targets);
        for(auto& vertex:vertices){vertex.position[1]+=0.006F;vertex.uv={{0,0}};}
        if(!vertices.empty())_productionRange=_renderer->mesh(vertices);
        vertices=g1::terrain_vertices(cursor);
        for(auto& vertex:vertices){vertex.position[1]+=0.009F;vertex.uv={{0,0}};}
        if(!vertices.empty())_cursor=_renderer->mesh(vertices);
    } else if(_returnCursor && !_moveMode) {
        g1::TerrainMesh cursor;cursor.position_scale=_scene->terrain.position_scale;
        for(const auto& quad:_controlQuads)
            if(quad.tile_x==_returnCursor->x && quad.tile_y==_returnCursor->y)cursor.quads.push_back(quad);
        auto vertices=g1::terrain_vertices(cursor);
        for(auto& vertex:vertices){vertex.position[1]+=0.006F;vertex.uv={{0,0}};}
        if(!vertices.empty())_cursor=_renderer->mesh(vertices);
    }
    ++_contentGeneration;
}

- (void)prepareBattle {
    if (!_session || !_session->battle()) return;
    const auto& pending = *_session->battle();
    if (!_supportPresentation || _supportPresentation->status().token!=pending.token) {
        if(pending.exchange.support_types.size()!=pending.exchange.support_units.size() ||
           pending.units_before.size()!=_session->units().size())
            throw g1::FormatError("Pending battle has an inconsistent support snapshot");
        for(std::size_t i=0;i<pending.exchange.support_units.size();++i) {
            const auto index=pending.exchange.support_units[i];
            if(index>=pending.units_before.size() || !pending.units_before[index].active ||
               _session->world().units.at(index).type!=pending.exchange.support_types[i])
                throw g1::FormatError("Support helper identity differs from its pre-combat unit");
        }
        if(!pending.exchange.support_units.empty() && !_supportAssets) {
            auto assets=std::make_unique<g1::SupportOverlayAssets>(g1::load_support_overlay_assets(*_archive));
            id<MTLTexture> texture=_renderer->texture(assets->image);
            _supportAssets=std::move(assets);_supportTexture=texture;
        }
        auto support=std::make_unique<g1::SupportPresentation>(pending.token,pending.exchange.support_units,
            _supportAssets ? _supportAssets->descriptor.animations().at(g1::support_effect_animation) :
                std::vector<g1::AtlasStep>{});
        std::vector<g1::MetalDraw> snapshots;
        if(!support->status().complete) {
            for(std::size_t i=0;i<_scene->sprites.size();++i) {
                const auto& sprite=_scene->sprites[i];
                const auto& unit=pending.units_before.at(sprite.unit_index);
                if(!unit.active)continue;
                std::size_t frame;
                if(_animations.at(i))frame=_animations[i]->current_frame();
                else {
                    // A new roster may be rebuilt after damage. Reconstruct
                    // its pre-combat idle pose without changing gameplay.
                    if(unit.form>1 || unit.facing>3)throw g1::FormatError("Invalid support sprite pose");
                    const auto type=_session->world().units.at(sprite.unit_index).type;
                    const auto animation=_animationTable.at(type).at(unit.form*4+unit.facing);
                    if(animation>=sprite.descriptor.animations().size())
                        throw g1::FormatError("Missing pre-combat support sprite animation");
                    frame=sprite.descriptor.animations()[animation].front().frame_index;
                }
                snapshots.push_back({_spriteMeshes.at(i).at(frame),[self spriteTexture:sprite.resource.image_id],
                    unit_model(_session->world(),unit),{{1,1,1,1}},true,{},false,false});
            }
        }
        _supportSavedCursor=_returnCursor;
        _supportSavedTitle=[_title.text copy];
        if(!_supportSavedCursor) {
            // The live core may already have deselected a lethal attacker.
            // Keep a presentation-only tile using its pre-combat snapshot.
            const auto selectedIndex=_session->selected().value_or(pending.attacker);
            const auto& selected=pending.units_before.at(selectedIndex);
            _supportSavedCursor=g1::WorldCoordinate{selected.tile_x,selected.tile_y};
        }
        _supportUnitDraws=std::move(snapshots);_supportPresentation=std::move(support);
        _supportDisplay.invalidate();
        // These are host selection visuals. The already-resolved core, its
        // selected unit, damage and random stream remain untouched.
        _range.reset();_cursor.reset();_attackRange.reset();_healRange.reset();_productionRange.reset();
        _moveMode=false;_attackMode=false;_healMode=false;
        _pendingDestination.reset();_productionMode.reset();_productionType.reset();_productionPlacement.reset();
        if(!_supportPresentation->status().complete) {
            ++_supportSequences;[self showSupportHelper];
            _clockStarted=false;_simulationClock.reset();++_contentGeneration;
            [self requestSupportTutorial];
        }
    }
    if(!_supportPresentation->status().complete)return;
    if (_battlePresentation && _battlePresentation->status().token == pending.token) return;
    g1::M3GBattleRequest request;
    request.token=pending.token; request.terrain_style=_session->world().style;
    request.attacker_before=pending.attacker_before; request.defender_before=pending.defender_before;
    request.exchange=pending.exchange;
    auto presentation=std::make_unique<g1::M3GBattlePresentation>(*_archive,std::move(request));
    std::vector<std::shared_ptr<g1::MetalM3GModel>> models;
    models.reserve(presentation->assets().size());
    _battleEnhancedTextures=0;
    for (std::size_t i=0;i<presentation->assets().size();++i) {
        const auto& asset=presentation->assets()[i];
        const auto kind=i==0?g1::MaterialEnhancement::environment:i<3?
            g1::MaterialEnhancement::character:g1::MaterialEnhancement::effect;
        auto gpu=_graphicsModern?_m3gRenderer->upload_modern(asset.render,kind):_m3gRenderer->upload(asset.render);
        _battleEnhancedTextures+=gpu->enhanced_texture_count();models.push_back(std::move(gpu));
    }
    // Publish only a complete original-asset/GPU set. Allocation or unsupported
    // state failures preserve the pending gameplay token and stop visibly.
    _battleModels=std::move(models); _battlePresentation=std::move(presentation);
    _supportCursor.reset();_supportUnitDraws.clear();_returnCursor=_supportSavedCursor;
    _title.text=_supportSavedTitle ?: [self originalText:715];
    _battlePresentedToken=0;
    [_battleCanvas resetPresentation];
    for (const auto request:_battlePresentation->take_audio_requests()) [self dispatchAudio:request];
    // Asset-loading time is not a simulation catch-up interval. The first
    // matching drawable must be shown before the 30ms battle clock advances.
    _clockStarted=false; _simulationClock.reset();
    ++_contentGeneration;
}

- (void)showSupportHelper {
    if(!_supportPresentation || _supportPresentation->status().complete || !_session->battle())return;
    const auto status=_supportPresentation->status();
    const auto index=status.helper_unit.value();
    const auto& unit=_session->battle()->units_before.at(index);
    const auto type=_session->world().units.at(index).type;
    const auto& definition=_session->world().unit_definitions.at(type);
    const auto primary=g1::script_text_id(g1::unit_text_index(definition.raw_text_indices.at(0)));
    _title.text=[NSString stringWithFormat:@"%@ %@",native_text(_text->at(primary)),
        native_text(_text->at(g1::support_text_id))];
    // Immediate focus uses the existing native inspection camera. Its easing
    // is not asserted to reproduce the original tactical camera.
    _camera.center_x=(unit.tile_x+0.5F)*0.32F;
    _camera.center_z=(unit.tile_y+0.5F)*0.32F;
    g1::TerrainMesh cursor;cursor.position_scale=_scene->terrain.position_scale;
    for(const auto& quad:_controlQuads)
        if(quad.tile_x==unit.tile_x && quad.tile_y==unit.tile_y)cursor.quads.push_back(quad);
    auto vertices=g1::terrain_vertices(cursor);
    for(auto& vertex:vertices){vertex.position[1]+=0.006F;vertex.uv={{0,0}};}
    _supportCursor=_renderer->mesh(vertices);
    _supportDisplay.invalidate();
    ++_contentGeneration;
}

- (void)requestSupportTutorial {
    if(!_campaign || !_session || !_session->battle() || !_supportPresentation ||
       _supportPresentation->status().complete)return;
    const auto& pending=*_session->battle();
    g1::SupportTutorialSlots owners{};
    if(pending.exchange.support_units.size()>owners.size())
        throw g1::FormatError("Support tutorial helper list exceeds original six slots");
    for(std::size_t i=0;i<pending.exchange.support_units.size();++i)
        owners[i]=_session->world().units.at(pending.exchange.support_units[i]).owner;
    const auto player=_session->current_player();
    const auto decision=g1::support_tutorial_entry(
        _campaign->profile().support_tutorial_pending ? 1U : 0U,player,
        _session->turn().controllers.at(player),owners);
    if(!decision.request_message)return;
    // Original code clears its profile flag before requesting this text.
    // Native policy also persists that change atomically before the popup;
    // a failed write keeps the pending battle and exposes an explicit retry.
    const auto written=_campaign->consume_support_tutorial();
    if(!written.succeeded) {
        _supportTutorialRetryRequired=true;
        [self showPanel:NativePanel::tutorial_save_error title:@"Tutorial progress could not be saved"
            body:native_text(written.error) token:++_localPanelToken];
        [self addCampaignButton:@"Retry save" action:@selector(retrySupportTutorial)];
        return;
    }
    _supportTutorialRetryRequired=false;
    _pendingMessageIds.push_back(g1::support_tutorial_text_resource);
    ++_supportTutorialRequests;
    [self showNextMessage];
}

- (void)retrySupportTutorial {
    if(!_supportTutorialRetryRequired || _panelPurpose!=NativePanel::tutorial_save_error)return;
    try {
        [self hidePanel];
        [self requestSupportTutorial];
        [self refreshControls];
    } catch(const std::exception& error) { [self fail:native_text(error.what())]; }
}

- (void)refreshControls {
    if (!_session || _failed) return;
    if(_panelPurpose!=NativePanel::none) {
        _commands.hidden=YES;_dialoguePanel.hidden=YES;_battlePanel.hidden=YES;
        _techSpecsHotspot.hidden=YES;return;
    }
    if (_lastMissionMode != _session->mission().interaction_mode) {
        _lastMissionMode = _session->mission().interaction_mode;
        _moveMode = false; _attackMode = false;_healMode=false;
        _productionMode.reset();_productionType.reset();_productionPlacement.reset();
        _pendingDestination.reset(); _rangeDirty = true;
    }
    if (_lastSelection != _session->selected()) {
        _lastSelection = _session->selected();
        _rangeDirty = true;
        _pendingDestination.reset();
        _moveMode = false;
        _attackMode = false;
        _healMode = false;
        _productionMode.reset();_productionType.reset();_productionPlacement.reset();
    }
    if(!_session->battle())[self refreshRange];
    const auto& mission = _session->mission();
    const bool battle = _session->battle().has_value();
    _battlePanel.hidden = !battle;
    const BOOL debugVisible=_showDiagnostics && !battle;
    _title.hidden=!debugVisible; _hint.hidden=!debugVisible; _worldLegend.hidden=!debugVisible;
    if (battle) {
        [self prepareBattle];
        _gameHudFrame.hidden=YES;  // Never composite the tactical strip underneath the battle plates.
        _commands.hidden=YES;_dialoguePanel.hidden=YES;
        // prepareBattle may have opened the original one-time explanation
        // or a retryable profile-save error. Preserve that modal's ownership.
        if(_panelPurpose!=NativePanel::none){_battlePanel.hidden=YES;return;}
        if(_supportPresentation && !_supportPresentation->status().complete) {
            _battlePanel.hidden=YES;_title.hidden=!_showDiagnostics;
            _hint.hidden=YES;_worldLegend.hidden=YES;
            return;
        }
        const auto status=_battlePresentation->status();
        const auto name = [&](std::size_t side) {
            const auto& definition = _session->world().unit_definitions.at(status.types[side]);
            return native_text(_text->at(g1::script_text_id(g1::unit_text_index(definition.raw_text_indices.at(status.forms[side])))));
        };
        [_battleCanvas showStatus:status left:name(0) right:name(1)
            leftFaction:_session->world().unit_definitions.at(status.types[0]).faction()
            rightFaction:_session->world().unit_definitions.at(status.types[1]).faction()];
        return;
    }
    const bool dialogue = mission.gate == g1::MissionGate::dialogue && mission.dialogue.has_value();
    _dialoguePanel.hidden = !dialogue || battle;
    _gameHudFrame.hidden=dialogue || battle;
    [self setSourceTopLeftDialogueMode:dialogue];
    _techSpecsHotspot.hidden = !_session->selected() || dialogue || battle ||
         mission.interaction_mode != 0 || _session->moving() || _session->focus_request() ||
         _session->world_presentation() || _session->turn().controllers[_session->current_player()]==1;
    _commands.hidden = battle || dialogue || mission.interaction_mode != 0 ||
        _session->moving() || _session->focus_request().has_value() || _session->world_presentation().has_value() ||
        _session->turn().controllers[_session->current_player()]==1;
    if (battle) {
        _hint.text = @"Battle";
    } else if (dialogue) {
        if (_dialogueCommand != mission.command_index) {
            _dialogueCommand = mission.command_index;
            _dialogueHeading.text = native_text(_text->at(mission.dialogue->heading_text_id));
            const auto portrait=g1::source_art::dialogue_portrait(_dialogueHeading.text);
            _dialoguePortrait.image=portrait.id ? _dialoguePortraits[@(portrait.id)] : nil;
            [_dialogueCanvas setOriginalText:native_text(_text->at(mission.dialogue->text_id))];
            _inputEdges = 0;
        }
        [_dialogueCanvas showProgress:mission.dialogue->progress];
        _dialoguePanel.accessibilityLabel=[NSString stringWithFormat:@"%@. %@. Tap to continue.",
            _dialogueHeading.text ?: @"",_dialogueCanvas.accessibilityLabel ?: @""];
        _hint.text = @"";
    } else if (mission.stop != g1::MissionStop::none) {
        _hint.text = @"The next part of this campaign is not available in this build. Details are in the test report.";
        _commands.hidden = YES;
    } else if (_notice && CACurrentMediaTime() < _noticeUntil) {
        _hint.text = _notice;
    } else if (_session->moving()) {
        _hint.text = @"Moving…";
    } else if (_session->turn().controllers[_session->current_player()]==1) {
        _hint.text = @"Computer turn";
    } else if (_session->selected()) {
        const auto index = *_session->selected();
        const auto& unit = _session->world().units.at(index);
        const auto& definition = _session->world().unit_definitions.at(unit.type);
        const auto& combat = _session->combat_units().at(index);
        const auto name = native_text(_text->at(g1::script_text_id(g1::unit_text_index(definition.raw_text_indices.at(unit.form)))));
        _hint.text = _productionMode ? @"Tap a highlighted adjacent tile, then tap it again to activate." :
            _attackMode ? @"Select a highlighted enemy to attack." : _healMode ? @"Select a highlighted friendly unit to repair." :
            _moveMode ? @"Tap a green tile, then tap it again to move." :
            [NSString stringWithFormat:@"%@ · HP %u · ATK %d–%d · DEF %d · Energy %d",name,unsigned(combat.health),
                int(combat.stats.attack_low),int(combat.stats.attack_high),int(combat.stats.defense),
                _session->turn().funds[_session->current_player()]];
    } else if (mission.objective) {
        _hint.text = native_text(_text->at(mission.objective->text_id));
    } else {
        _hint.text = @"Select a unit. Drag or pinch to move the view.";
    }
    _moveButton.enabled = _previewRange && _previewRange->has_unoccupied_destination && !_session->moving();
    _captureButton.enabled = _session->selected_can_capture();
    _transformButton.enabled = _session->selected_transform_eligibility() == g1::TransformEligibility::allowed;
    _attackButton.enabled = !_attackTargets.empty() && !battle;
    _healButton.enabled=!_healTargets.empty() && !battle;
    _upgradeButton.enabled=_session->selected_upgrade_eligibility()==g1::UpgradeEligibility::allowed;
    _endTurnButton.enabled=_session->end_turn_available();
    _buildButton.enabled=_session->selected_production_eligibility(g1::ProductionMode::ordinary)==g1::ProductionEligibility::allowed;
    _deployButton.enabled=_session->selected_production_eligibility(g1::ProductionMode::deploy)==g1::ProductionEligibility::allowed;
    _undoButton.enabled=_session->selected_can_undo();
    // Upgrade cost is spoken by VoiceOver; never overprint the original icon.
    if(_session->selected()) {
        const auto& unit=_session->world().units.at(*_session->selected());
        const auto cost=g1::unit_spawn_cost(_session->world().unit_definitions.at(unit.type));
        _upgradeButton.accessibilityValue=[NSString stringWithFormat:@"%d Energon",int(cost)];
    } else _upgradeButton.accessibilityValue=nil;
    update_original_action_state(_moveButton,_moveMode);
    update_original_action_state(_attackButton,_attackMode);
    update_original_action_state(_healButton,_healMode);
    for(UIButton* button in @[_captureButton,_transformButton,_upgradeButton,_endTurnButton,
                             _buildButton,_deployButton,_undoButton])
        update_original_action_state(button,NO);
    // Original bottom status bar follows selected unit and faction money.
    // Reading gameplay state here does NOT submit commands or change saves.
    const auto funds=_session->turn().funds[_session->current_player()];
    if(_session->selected()) {
        const auto index=*_session->selected();
        const auto& unit=_session->world().units.at(index);
        const auto& definition=_session->world().unit_definitions.at(unit.type);
        const auto& combat=_session->combat_units().at(index);
        NSString* name=native_text(_text->at(g1::script_text_id(g1::unit_text_index(
            definition.raw_text_indices.at(unit.form)))));
        // Vehicle and robot icons switch with the real selected form.
        [_transformButton setImage:unit.form ? _robotTransformIcon : _vehicleTransformIcon
            forState:UIControlStateNormal];
        [_transformButton setImage:unit.form ? _robotTransformDisabledIcon : _vehicleTransformDisabledIcon
            forState:UIControlStateDisabled];
        [_gameHudFrame displayUnit:name health:combat.health attackLow:combat.stats.attack_low
             attackHigh:combat.stats.attack_high armor:combat.stats.defense
             speed:_session->world().movement_profile(index).base_budget funds:funds];
    } else {
        NSString* title=_session->world().title_text_index ?
            native_text(_text->at(g1::script_text_id(*_session->world().title_text_index))) : @"G1 AWAKENING";
        NSString* objective=mission.objective ? native_text(_text->at(mission.objective->text_id)) : @"";
        [_gameHudFrame displayMission:title objective:objective funds:funds];
    }
    // Both commands can be valid in some states but physically share a
    // source slot. Prefer deploy only when it is the sole eligible action.
    const BOOL deploying=_deployButton.enabled && !_buildButton.enabled;
    _deployButton.hidden=!deploying;
    _buildButton.hidden=deploying;
    if (_session->world().title_text_index)
        _title.text = native_text(_text->at(g1::script_text_id(*_session->world().title_text_index)));
}

// The original character panel is read-only. Its price and both forms are
// decoded from the same canonical unit definition as the gameplay core.
- (void)renderCurrentTechSpecs {
    if(!_session || _techSpecsPosition>=_techSpecsRoster.size() || !_text)return;
    const std::size_t index=_techSpecsRoster[_techSpecsPosition];
    const auto& world=_session->world();
    if(index>=world.units.size() || index>=_session->combat_units().size())return;
    const auto& unit=world.units[index];
    const auto& definition=world.unit_definitions.at(unit.type);
    const auto& combat=_session->combat_units().at(index);
    const auto stats=g1::read_tech_specs(definition,combat.level);
    // Extend only the original faction grid under any safe-area / window surplus.
    _techSpecsPanel.backgroundColor=[UIColor colorWithPatternImage:
        g1::source_art::png(*_archive,stats.faction==0?874:799,128,63)];
    const auto asText=[&](std::uint16_t raw) {
        return native_text(_text->at(g1::script_text_id(g1::unit_text_index(raw))));
    };
    NSString* name=asText(definition.raw_text_indices[0]);
    NSString* quote=asText(definition.raw_text_indices[1]);
    NSMutableArray<NSString*>* features=[NSMutableArray new];
    const auto& form=stats.form[0];
    NSString* range=form.min_range==form.max_range ?
        [NSString stringWithFormat:@"ATTACK RANGE: %u",unsigned(form.max_range)] :
        [NSString stringWithFormat:@"ATTACK RANGE: %u-%u",unsigned(form.min_range),unsigned(form.max_range)];
    [features addObject:range];
    // The original PC screenshots supplied by the owner confirm these exact
    // abilities for Bumblebee and Frenzy. All strings come from source FGIB
    // localization entries; no approximate descriptions are manufactured.
    if(definition.type==11) {
        for(const std::uint16_t id:{807,714,929})[features addObject:native_text(_text->at(id))];
    }else if(definition.type==19) {
        for(const std::uint16_t id:{807,929,1174})[features addObject:native_text(_text->at(id))];
    } else {
        for(auto raw:definition.raw_detail_text_indices) {
            NSString* detail=asText(raw);
            if([detail isEqualToString:@"ATTACK RANGE: "] ||
               [detail isEqualToString:@"NEXT LEVEL UNLOCKED!"] ||
               ([detail isEqualToString:@"CANNOT ATTACK"] && form.combat.attack_low>0))continue;
            [features addObject:detail];
        }
    }
    std::uint32_t portraitId=0;
    if(definition.type==11)portraitId=731;
    else if(definition.type==19)portraitId=646;
    else if(definition.type==0)portraitId=878;
    else if(definition.type==2)portraitId=670;
    [_techSpecsCanvas show:stats name:name quote:quote details:features
                   portrait:portraitId?_techSpecsPortraits[@(portraitId)]:nil];
    // Default to the actual current form when moving between Transformers.
    // Touching a form icon previews it without executing Transform or saving.
    _techSpecsPreviewForm=std::min<std::uint8_t>(unit.form,stats.forms-1);
    _specsVehicleButton.enabled=stats.forms>1;
    _specsVehicleButton.accessibilityValue=stats.forms>1?@"Available":@"Unavailable";
    if(![_techSpecsModel displayUnit:unit.type form:_techSpecsPreviewForm archive:*_archive]) {
        _techSpecsCanvas.accessibilityHint=[NSString stringWithFormat:
             @"Original M3G preview unavailable: %@",[_techSpecsModel lastModelError]];
    } else _techSpecsCanvas.accessibilityHint=nil;
}
- (void)openTechSpecs {
    if(!_session || !_archive || !_text || !_loaded || _failed || _typeListOpen ||
        (_panelPurpose!=NativePanel::none && _panelPurpose!=NativePanel::pause) ||
        _session->battle() || _session->moving() || _session->focus_request() ||
        _session->mission().gate==g1::MissionGate::dialogue)return;
    try {
        std::vector<std::size_t> roster;
        const auto& units=_session->world().units;
        for(std::size_t n=0;n<units.size();++n)
            if(units[n].active && units[n].type<_session->world().unit_definitions.size())
                roster.push_back(n);
        if(roster.empty())return;
        _techSpecsFromPause=_panelPurpose==NativePanel::pause;
        _techSpecsRoster=std::move(roster);
        _techSpecsPosition=0;
        if(_session->selected()){
            const auto it=std::find(_techSpecsRoster.begin(),_techSpecsRoster.end(),*_session->selected());
            if(it!=_techSpecsRoster.end())_techSpecsPosition=std::distance(_techSpecsRoster.begin(),it);
        }
        if(_techSpecsFromPause)_campaignPanel.hidden=YES;
        _panelPurpose=NativePanel::tech_specs;
        _inputEdges=0;_simulationClock.reset();_clockStarted=false;
        _metalView.paused=YES;
        _gameHudFrame.hidden=YES;_menuButton.hidden=YES;_techSpecsHotspot.hidden=YES;
        _commands.hidden=YES;_dialoguePanel.hidden=YES;_battlePanel.hidden=YES;
        _fps.hidden=YES;_title.hidden=YES;_hint.hidden=YES;_worldLegend.hidden=YES;
        _techSpecsPanel.hidden=NO;
        [self.view bringSubviewToFront:_techSpecsPanel];
        [self layoutOriginalGameplayUI];
        [self renderCurrentTechSpecs];
    }catch(const std::exception& error){[self fail:native_text(error.what())];}
}
- (void)closeTechSpecs {
    if(_panelPurpose!=NativePanel::tech_specs)return;
    _techSpecsPanel.hidden=YES;
    _techSpecsRoster.clear();
    if(_techSpecsFromPause){_techSpecsFromPause=NO;[self showPauseMenu];return;}
    _panelPurpose=NativePanel::none;
    _simulationClock.reset();_clockStarted=false;
    if(_session)_previousUnits=_session->units();
    // restore the original always-visible tactical shell; refreshControls
    // updates command availability but intentionally does not own these views.
    _menuButton.hidden=NO;
    _gameHudFrame.hidden=NO;
    if(_loaded && !_failed)_metalView.paused=NO;
    [self refreshControls];
}
- (void)nextTechSpecs {
    if(_panelPurpose!=NativePanel::tech_specs || _techSpecsRoster.empty())return;
    _techSpecsPosition=(_techSpecsPosition+1)%_techSpecsRoster.size();
    [self renderCurrentTechSpecs];
}
- (void)previousTechSpecs {
    if(_panelPurpose!=NativePanel::tech_specs || _techSpecsRoster.empty())return;
    _techSpecsPosition=(_techSpecsPosition+_techSpecsRoster.size()-1)%_techSpecsRoster.size();
    [self renderCurrentTechSpecs];
}
- (void)showTechRobot {
    if(_panelPurpose!=NativePanel::tech_specs || _techSpecsRoster.empty())return;
    const auto type=_session->world().units.at(_techSpecsRoster.at(_techSpecsPosition)).type;
    _techSpecsPreviewForm=0;
    [_techSpecsModel displayUnit:type form:0 archive:*_archive];
}
- (void)showTechVehicle {
    if(_panelPurpose!=NativePanel::tech_specs || _techSpecsRoster.empty())return;
    const auto& unit=_session->world().units.at(_techSpecsRoster.at(_techSpecsPosition));
    if(_session->world().unit_definitions.at(unit.type).forms.size()<2)return;
    _techSpecsPreviewForm=1;
    [_techSpecsModel displayUnit:unit.type form:1 archive:*_archive];
}

- (void)beginMove {
    if (!_loaded || _failed) return;
    try {
        _moveMode = true;
        _attackMode = false;
        _healMode = false;
        _productionMode.reset();_productionType.reset();_productionPlacement.reset();_returnCursor.reset();
        _pendingDestination.reset();
        _rangeDirty = true;
        _notice = nil;
        [self refreshControls];
    } catch (const std::exception& e) { [self fail:native_text(e.what())]; }
}

- (void)beginAttack {
    if (!_loaded || _failed || _session->battle()) return;
    try {
        _attackMode = !_session->selected_attack_targets().empty();
        _moveMode = false;
        _healMode = false;
        _productionMode.reset();_productionType.reset();_productionPlacement.reset();_returnCursor.reset();
        _pendingDestination.reset();
        _rangeDirty = true;
        _notice = nil;
        [self refreshControls];
    } catch (const std::exception& e) { [self fail:native_text(e.what())]; }
}

- (void)beginHeal {
    if(!_loaded || _failed || _panelPurpose!=NativePanel::none || _session->battle())return;
    try {
        _healMode=!_session->selected_heal_targets().empty();
        _productionMode.reset();_productionType.reset();_productionPlacement.reset();_returnCursor.reset();
        _attackMode=false;_moveMode=false;_pendingDestination.reset();_rangeDirty=true;_notice=nil;
        [self refreshControls];
    } catch(const std::exception& e){[self fail:native_text(e.what())];}
}

- (BOOL)productionTileAllowed:(g1::WorldCoordinate)tile {
    if(!_productionMode || !_productionPlacement || !_session->selected())return NO;
    const auto& unit=_session->units().at(*_session->selected());
    const int dx=int(tile.x)-int(unit.tile_x),dy=int(tile.y)-int(unit.tile_y);
    const std::array<std::array<int,2>,4> directions{{{{0,-1}},{{0,1}},{{-1,0}},{{1,0}}}};
    for(std::size_t i=0;i<directions.size();++i)
        if(dx==directions[i][0] && dy==directions[i][1])
            return _productionPlacement->reachable[i] && _productionPlacement->vacant[i];
    return NO;
}

// Some original units can simultaneously expose production and tape deploy.
// Preserve BOTH executable actions when one source icon must occupy a bank
// slot. The temporary native action sheet is intentionally restricted to
// that ambiguous state; it never alters a unit's production eligibility.
- (void)beginProductionOrDeploy {
    if(_buildButton.enabled && _deployButton.enabled) {
        UIAlertController* choice=[UIAlertController alertControllerWithTitle:@"Activate"
             message:@"Select available unit activation" preferredStyle:UIAlertControllerStyleActionSheet];
        __weak G1ViewController* owner=self;
        [choice addAction:[UIAlertAction actionWithTitle:@"Activate unit"
            style:UIAlertActionStyleDefault handler:^(UIAlertAction* action) {
                (void)action; [owner beginProduction];
            }]];
        [choice addAction:[UIAlertAction actionWithTitle:@"Activate tape"
            style:UIAlertActionStyleDefault handler:^(UIAlertAction* action) {
                (void)action; [owner beginDeploy];
            }]];
        [choice addAction:[UIAlertAction actionWithTitle:@"Cancel"
            style:UIAlertActionStyleCancel handler:nil]];
        choice.popoverPresentationController.sourceView=_buildButton;
        choice.popoverPresentationController.sourceRect=_buildButton.bounds;
        [self presentViewController:choice animated:YES completion:nil];
    }else if(_buildButton.enabled)[self beginProduction];
    else if(_deployButton.enabled)[self beginDeploy];
}
- (void)beginProduction { [self showProductionTypes:g1::ProductionMode::ordinary]; }
- (void)beginDeploy { [self showProductionTypes:g1::ProductionMode::deploy]; }
- (void)cancelProduction {
    _presentationEpoch.invalidate();
    _typeListOpen=false;_productionMode.reset();_productionType.reset();_productionPlacement.reset();
    _pendingDestination.reset();_rangeDirty=true;_clockStarted=false;_simulationClock.reset();
    [self refreshControls];
}

- (void)showProductionTypes:(g1::ProductionMode)mode {
    if(!_loaded || _failed || _panelPurpose!=NativePanel::none || _typeListOpen)return;
    try {
        if(_session->selected_production_eligibility(mode)!=g1::ProductionEligibility::allowed ||
           !_session->selected())return;
        const auto candidates=_session->selected_production_candidates(mode);
        if(candidates.empty())return;
        const auto producer=*_session->selected();
        const auto spawn=_session->unit_spawn_generation(producer),session=_sessionInstalls;
        const int funds=_session->turn().funds[_session->current_player()];
        UIAlertController* choices=[UIAlertController alertControllerWithTitle:[self originalText:
            mode==g1::ProductionMode::deploy ? 1055 : 785]
            message:[NSString stringWithFormat:@"Energy: %d",funds]
            preferredStyle:UIAlertControllerStyleActionSheet];
        __weak G1ViewController* weakSelf=self;
        for(const auto type:candidates) {
            const auto& definition=_session->world().unit_definitions.at(type);
            const auto name=[self originalText:g1::script_text_id(g1::unit_text_index(definition.raw_text_indices[0]))];
            const auto cost=g1::unit_spawn_cost(definition);
            NSString* label=[NSString stringWithFormat:@"%@ · %d",name,int(cost)];
            [choices addAction:[UIAlertAction actionWithTitle:label style:UIAlertActionStyleDefault
                handler:^(UIAlertAction* action){(void)action;
                    [weakSelf selectProductionType:type mode:mode session:session producer:producer spawn:spawn];}]];
        }
        [choices addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel
            handler:^(UIAlertAction* action){(void)action;[weakSelf cancelProduction];}]];
        choices.popoverPresentationController.sourceView=_commands;
        choices.popoverPresentationController.sourceRect=_commands.bounds;
        _typeListOpen=true;_simulationClock.reset();_presentationEpoch.invalidate();
        [self presentViewController:choices animated:YES completion:nil];
        choices.presentationController.delegate=self;
    } catch(const std::exception& e){[self fail:native_text(e.what())];}
}

- (void)selectProductionType:(std::uint8_t)type mode:(g1::ProductionMode)mode
    session:(std::uint64_t)session producer:(std::size_t)producer spawn:(std::uint64_t)spawn {
    if(session!=_sessionInstalls || !_session || !_session->selected() ||
       producer>=_session->units().size() || *_session->selected()!=producer ||
       _session->unit_spawn_generation(producer)!=spawn)return;
    _typeListOpen=false;_clockStarted=false;_simulationClock.reset();_presentationEpoch.invalidate();
    try {
        const auto candidates=_session->selected_production_candidates(mode);
        if(std::find(candidates.begin(),candidates.end(),type)==candidates.end())
            throw g1::FormatError("Selected activation type no longer belongs to the current menu");
        _productionPlacement=_session->selected_production_placement(mode);
        if(!_productionPlacement)return;
        _productionMode=mode;_productionType=type;_pendingDestination=_productionPlacement->initial_cursor;
        _moveMode=false;_attackMode=false;_healMode=false;_returnCursor.reset();_notice=nil;_rangeDirty=true;
        [self refreshControls];
    } catch(const std::exception& e){[self fail:native_text(e.what())];}
}

- (void)presentationControllerDidDismiss:(UIPresentationController*)presentationController {
    (void)presentationController;
    if(_typeListOpen)[self cancelProduction];
}

- (void)upgradeUnit {
    if(!_loaded || _failed || _panelPurpose!=NativePanel::none)return;
    try {
        const auto result=_session->upgrade_selected();
        if(result) {
            ++_upgradeActions;_checkpointRequested=true;_rangeDirty=true;
            [self dispatchAudio:g1::MissionAudioRequest{5,13}]; // Original human59edc branch.
            _moveMode=false;_attackMode=false;_healMode=false;_pendingDestination.reset();
            _notice=[NSString stringWithFormat:@"Level %u · Energy %d",unsigned(result->combat.level),result->funds_after];
            _noticeUntil=CACurrentMediaTime()+3;
        }
        [self refreshControls];[self saveCheckpoint];
    } catch(const std::exception& e){[self fail:native_text(e.what())];}
}

- (void)undoAction {
    if(!_loaded || _failed || _panelPurpose!=NativePanel::none || _typeListOpen)return;
    try {
        const auto result=_session->undo_selected();
        if(result) {
            ++_undoActions;_checkpointRequested=true;_rangeDirty=true;
            _moveMode=false;_attackMode=false;_healMode=false;
            _productionMode.reset();_productionType.reset();_productionPlacement.reset();
            _pendingDestination.reset();_returnCursor=result->cursor;
            _previousUnits=_session->units();
            [self dispatchAudio:g1::MissionAudioRequest{result->audio_operation,result->audio_argument}];
            _notice=nil;++_contentGeneration;
            // GameSession owns retain_selection and the record=false form
            // reversal. Do not synthesize a second transform animation here.
        }
        [self refreshControls];[self saveCheckpoint];
    } catch(const std::exception& e){[self fail:native_text(e.what())];}
}

- (void)endTurn {
    if(!_loaded || _failed || _panelPurpose!=NativePanel::none || _endTurnButton.hidden)return;
    try {
        const auto result=_session->end_turn();
        if(result) {
            ++_endTurnActions;_checkpointRequested=true;_rangeDirty=true;
            _moveMode=false;_attackMode=false;_healMode=false;_pendingDestination.reset();_notice=nil;
        }
        [self refreshControls];
    } catch(const std::exception& e){[self fail:native_text(e.what())];}
}

- (void)finishBattleFrame:(const NativePresentedFrame::Battle&)frame {
    if (!_loaded || _failed || !_session->battle() || !_battlePresentation || !frame.complete) return;
    const auto status=_battlePresentation->status();
    if (frame.token!=_session->battle()->token || frame.token!=status.token ||
        frame.revision!=status.revision || !status.complete ||
        ![_battleCanvas hasDrawnToken:frame.token revision:frame.revision]) return;
    // Called only from this drawable's positive presentedTime callback. The
    // original controller has finished and the same final HUD revision drew.
    // Neither a timer, a Continue button nor an unsupported-asset path can ack.
    const auto battle=*_session->battle();
    _session->report_battle_finished(battle.token);
    _completedBattles.push_back(battle);
    if (_completedBattles.size()>64) _completedBattles.erase(_completedBattles.begin());
    ++_battleAcknowledgements;
    _lastBattleFinalToken=frame.token; _lastBattleFinalRevision=frame.revision;
    // Original45af0 natural-completion wrapper calls audio operation2,arg0.
    // This conditional query is not an invented music-resume/loop request.
    [self dispatchAudio:g1::MissionAudioRequest{2,0}];
    _battlePresentation.reset(); _battleModels.clear(); _battlePresentedToken=0;
    _supportPresentation.reset();_supportUnitDraws.clear();_supportCursor.reset();
    _supportSavedCursor.reset();_supportDisplay.invalidate();
    _supportSavedTitle=nil;
    _checkpointRequested=true; _rangeDirty=true; _attackMode=false; _notice=nil;
    _clockStarted=false; _simulationClock.reset(); ++_contentGeneration;
    [self refreshControls];
}

- (void)capture {
    if (!_loaded || _failed) return;
    try {
        const auto result = _session->capture_selected();
        if (result.status == g1::CaptureStatus::captured) {
            ++_captureActions;
            _checkpointRequested=true;
            if (result.audio_5_12) [self dispatchAudio:g1::MissionAudioRequest{5,12}];
            _notice = @"Captured.";
            _noticeUntil = CACurrentMediaTime()+2;
            _rangeDirty = true;
            _pendingDestination.reset();
            _moveMode = false;
            _attackMode = false;
            _healMode = false;
            ++_contentGeneration;
        } else if (result.message_id) {
            _pendingMessageIds.push_back(*result.message_id);
        }
        [self refreshControls];
    } catch (const std::exception& e) { [self fail:native_text(e.what())]; }
}

- (void)transformUnit {
    if (!_loaded || _failed) return;
    try {
        const auto result = _session->transform_selected();
        if (result) {
            ++_transformActions;
            _checkpointRequested=true;
            if (result->audio_event_in_group5)
                [self dispatchAudio:g1::MissionAudioRequest{5,*result->audio_event_in_group5}];
            _rangeDirty = true;
            _pendingDestination.reset();
            _moveMode = false;
            _attackMode = false;
            _healMode = false;
            _notice = nil;
        }
        [self refreshControls];
    } catch (const std::exception& e) { [self fail:native_text(e.what())]; }
}

- (void)queueDialogueEdge:(std::uint32_t)edge {
    if (!_loaded || _failed || _session->mission().gate != g1::MissionGate::dialogue) return;
    _inputCommand = _session->mission().command_index;
    _inputEdges |= edge;
}
- (void)dialogueBack { [self queueDialogueEdge:1]; }
- (void)dialogueNext { [self queueDialogueEdge:16]; }
- (void)dialogueSkip { [self queueDialogueEdge:128]; }

- (void)dispatchAudio:(g1::MissionAudioRequest)request {
    ++_audioRequests;
    // The return value is the original query/operation result, not generic
    // output success. Actual framework/cache failures are in diagnostics.
    [_audio request:request];
}

- (void)maintainMusicAfterStep:(g1::MusicMaintenanceSite)site {
    if (!_loaded || _failed || !_session || _panelPurpose!=NativePanel::none || _typeListOpen ||
        _session->mission().stop!=g1::MissionStop::none) return;
    const bool mainBattle=_session->battle() && _battlePresentation &&
        _battlePresentation->status().token==_session->battle()->token;
    if ((site==g1::MusicMaintenanceSite::map_update && mainBattle) ||
        (site==g1::MusicMaintenanceSite::ordinary_battle && !mainBattle)) return;
    // Native active-session dispatch corresponds to the original app mode100.
    // Results, menus and pending campaign handoffs are excluded above; they
    // must not restart terrain music while their own presentation is active.
    const auto plan=g1::plan_music_maintenance({site,[_audio musicEnabled]!=NO,
        _session->mission().interaction_mode,100},_session->world().style);
    if (!plan.query_playing) return;
    ++_audioRequests;
    // Query actual output after this update's sound events. Any active voice,
    // including an effect, postpones the original terrain-music request.
    const bool playing=[_audio request:g1::MissionAudioRequest{6,0}];
    if (!playing && plan.when_idle) [self dispatchAudio:*plan.when_idle];
}

- (NSString*)messageText:(std::uint32_t)resource {
    if((resource&0xffff0000U)==0x21ff0000U)
        return [self originalText:static_cast<std::uint16_t>(resource)];
    if(resource<=65535U && _text->contains(static_cast<std::uint16_t>(resource)))
        return [self originalText:static_cast<std::uint16_t>(resource)];
    throw g1::FormatError("Requested localized message has no recovered text resource");
}

- (void)showNextMessage {
    if(_panelPurpose!=NativePanel::none || _pendingMessageIds.empty())return;
    [self showPanel:NativePanel::message title:[self originalText:715]
        body:[self messageText:_pendingMessageIds.front()] token:++_localPanelToken];
    _panelContinueButton=[self addCampaignButton:[self originalText:587] action:@selector(dismissMessage)];
    _panelContinueButton.enabled=NO;
}

- (void)dismissMessage {
    if(_panelPurpose!=NativePanel::message || _pendingMessageIds.empty() ||
       [_campaignCanvas drawnToken]!=_panelToken)return;
    _presentedMessageIds.push_back(_pendingMessageIds.front());
    if(_presentedMessageIds.size()>256)_presentedMessageIds.erase(_presentedMessageIds.begin());
    _pendingMessageIds.erase(_pendingMessageIds.begin());
    [self hidePanel];[self showNextMessage];[self refreshControls];
}

- (void)showResults {
    const auto& state=_campaign->state();
    if(!state.results)throw g1::FormatError("Campaign results request is missing its real record");
    const auto& result=*state.results;
    NSString* missionName=_session && _session->world().title_text_index ?
        native_text(_text->at(g1::script_text_id(*_session->world().title_text_index))) : @"";
    NSMutableString* body=[NSMutableString stringWithFormat:@"%@%d: %@\n\n%@ %d\n%@ %d\n%@ %d",
        [self originalText:784],result.campaign.mission_index+1,missionName,
        [self originalText:1217],result.current_record.value_278,
        [self originalText:941],result.current_record.value_27c,
        [self originalText:667],result.current_record.value_274];
    if(!result.finished_campaign)[body appendFormat:@"\n\n%@",[self originalText:1321]];
    // Never advertise a save until CampaignController confirms the filesystem write.
    if(state.profile_persisted)[body appendFormat:@"\n%@",[self originalText:462]];
    [self showPanel:NativePanel::results title:[self originalText:1323] body:body token:state.token];
    _panelContinueButton=[self addCampaignButton:[self originalText:587] action:@selector(continueResults)];
    _panelContinueButton.enabled=NO;
}

- (void)continueResults {
    if(!_campaign || _panelPurpose!=NativePanel::results ||
       _campaign->state().gate!=g1::CampaignControllerGate::results ||
       _campaign->state().token!=_panelToken || !_campaign->state().results ||
       !_campaign->state().results->drawn || [_campaignCanvas drawnToken]!=_panelToken)return;
    _resultsInputEdges|=16; // Actual native button, original accepted results mask.
}

- (void)retryCampaignSave {
    if(!_campaign)return;
    try {
        if(_campaign->state().gate==g1::CampaignControllerGate::save_failed) {
            const auto written=_campaign->retry_save(_campaign->state().token);
            if(!written.succeeded)_panelPurpose=NativePanel::none; // Refresh the actual retry error.
        }
        _campaignError=nil;
        _campaignRetryRequired=false;
        [self updateCampaign:0];
    } catch(const std::exception& e) {
        _campaignError=native_text(e.what());_campaignRetryRequired=true;
        [self showPanel:NativePanel::save_error title:@"Campaign could not continue"
            body:_campaignError token:++_localPanelToken];
        [self addCampaignButton:@"Retry" action:@selector(retryCampaignSave)];
    }
}

- (void)updateCampaign:(std::uint32_t)delta {
    if(!_campaign || !_session || _campaignRetryRequired)return;
    if(_panelPurpose==NativePanel::message) {
        _panelContinueButton.enabled=[_campaignCanvas drawnToken]==_panelToken;return;
    }
    if(_campaign->state().gate==g1::CampaignControllerGate::idle) {
        if(_panelPurpose!=NativePanel::none || !_pendingMessageIds.empty() ||
           _session->mission().stop!=g1::MissionStop::campaign_handoff_required)return;
        (void)_campaign->begin(*_session); // Writes the real profile; failures retain the old checkpoint.
    }
    using Gate=g1::CampaignControllerGate;
    const auto token=_campaign->state().token;
    if(_campaign->state().gate==Gate::save_failed) {
        if(_panelPurpose!=NativePanel::save_error || _panelToken!=token) {
            [self showPanel:NativePanel::save_error title:@"Campaign could not be saved"
                body:native_text(_campaign->state().save_error) token:token];
            [self addCampaignButton:@"Retry save" action:@selector(retryCampaignSave)];
        }
        return;
    }
    if(_campaign->state().gate==Gate::results) {
        if(_panelPurpose!=NativePanel::results || _panelToken!=token)[self showResults];
        if([_campaignCanvas drawnToken]==token && !_campaign->state().results->drawn) {
            const auto height=static_cast<std::int32_t>(std::ceil(_campaignCanvas.bounds.size.height));
            _campaign->report_results_presented(token,height);++_resultsReports;
        }
        if(_campaign->state().results->drawn) {
            _panelContinueButton.enabled=YES;
            const auto update=_campaign->update_results(token,delta ? _resultsInputEdges : 0U,delta);
            if(delta)_resultsInputEdges=update.remaining_input_edges;
            [_campaignCanvas setBackgroundOffset:update.background_offset_q16];
        }
        if(_campaign->state().gate==Gate::results)return;
    }
    try {
        if(_campaign->state().gate==Gate::next_mission) {
            auto prepared=_campaign->prepare_next_campaign(token,*_archive);
            auto session=std::make_unique<g1::GameSession>(*_archive,std::move(prepared.bootstrap),prepared.context);
            auto nextController=std::make_unique<g1::CampaignController>(_profilePath.UTF8String);
            [self installSession:std::move(session)];
            if(![self retireCheckpoint])throw std::runtime_error(_checkpointError.UTF8String ?: "Checkpoint retirement failed");
            _campaign->report_next_session_installed(token,*_session);
            _campaign=std::move(nextController);
            _checkpointRequested=true;_restoredCheckpoint=false;
            [self hidePanel];[self refreshControls];return;
        }
        if(_campaign->state().gate==Gate::campaign_menu) {
            if(![self retireCheckpoint])throw std::runtime_error(_checkpointError.UTF8String ?: "Checkpoint retirement failed");
            if(_panelPurpose!=NativePanel::menu || _panelToken!=token)[self showProfileMenu];
            if([_campaignCanvas drawnToken]==token)_campaign->report_menu_presented(token);
            return;
        }
        if(_campaign->state().gate==Gate::campaign_ending) {
            if(![self retireCheckpoint])throw std::runtime_error(_checkpointError.UTF8String ?: "Checkpoint retirement failed");
            if(!_campaign->state().handoff || !_campaign->state().handoff->ending_text_resource)
                throw g1::FormatError("Campaign ending has no original text request");
            if(_panelPurpose!=NativePanel::ending || _panelToken!=token) {
                [self showPanel:NativePanel::ending title:[self originalText:715]
                    body:[self messageText:*_campaign->state().handoff->ending_text_resource] token:token];
                _panelContinueButton=[self addCampaignButton:[self originalText:1169] action:@selector(showProfileMenu)];
                _panelContinueButton.enabled=NO;
            }
            if([_campaignCanvas drawnToken]==token) {
                _campaign->report_ending_presented(token);++_endingReports;_panelContinueButton.enabled=YES;
            }
        }
    } catch(const std::exception& e) {
        _campaignError=native_text(e.what());_campaignRetryRequired=true;
        [self showPanel:NativePanel::save_error title:@"Campaign could not continue"
            body:_campaignError token:++_localPanelToken];
        [self addCampaignButton:@"Retry" action:@selector(retryCampaignSave)];
    }
}

- (void)advanceCamera:(std::uint32_t)delta {
    const auto& mission = _session->mission();
    std::uint64_t token = 0;
    bool sessionFocus = false, immediate = false;
    std::int32_t x = 0, y = 0;
    if (mission.gate == g1::MissionGate::camera && mission.camera) {
        token = mission.command_index;
        x = mission.camera->x; y = mission.camera->y; immediate = mission.camera->immediate;
    } else if (_session->focus_request()) {
        const auto& request = *_session->focus_request();
        token = request.token; sessionFocus = true;
        x = request.tile.x; y = request.tile.y; immediate = request.immediate;
    } else { _cameraPan.reset(); return; }
    if (!_cameraPan || _cameraPan->token != token || _cameraPan->session_focus != sessionFocus) {
        if (x < 0 || y < 0 || x >= _session->world().grid.width || y >= _session->world().grid.height)
            throw g1::FormatError("Camera request lies outside the recovered scene");
        _cameraPan = NativeCameraPan{token,sessionFocus,_camera.center_x,_camera.center_z,
            (x+0.5F)*0.32F,(y+0.5F)*0.32F,0,immediate ? 0U : 360U,false};
    }
    auto& pan = *_cameraPan;
    if (pan.reached) return;
    pan.elapsed = std::min(pan.elapsed+delta,pan.duration);
    const float t = pan.duration ? static_cast<float>(pan.elapsed)/pan.duration : 1.0F;
    const float smooth = t*t*(3-2*t);
    _camera.center_x = pan.from_x+(pan.to_x-pan.from_x)*smooth;
    _camera.center_z = pan.from_z+(pan.to_z-pan.from_z)*smooth;
    pan.reached = pan.elapsed == pan.duration;
    ++_contentGeneration;
}

- (void)advanceSprites:(std::uint32_t)delta {
    std::vector<std::size_t> order;
    for (std::size_t i=0;i<_scene->sprites.size();++i) order.push_back(i);
    std::sort(order.begin(),order.end(),[&](std::size_t a,std::size_t b) {
        return _session->world().units[_scene->sprites[a].unit_index].type <
               _session->world().units[_scene->sprites[b].unit_index].type;
    });
    // Original6cd20 stores one shared delta for the entire ascending-type loop.
    auto sharedDelta = delta;
    for (const auto i : order) {
        const auto index = _scene->sprites[i].unit_index;
        const auto& unit = _session->units().at(index);
        const auto& worldUnit = _session->world().units.at(index);
        if (!unit.active) {
            _renderedActive[i] = false; _animations[i].reset(); _animationIds[i] = -1;
            _playbackTransformGenerations[i] = 0; _pendingTransformCompletions[i].reset();
            continue;
        }
        const auto& row = _animationTable.at(worldUnit.type);
        const auto signedId = [](std::uint8_t value) { return value < 128 ? int(value) : int(value)-256; };
        if (!_animations[i] || !_renderedActive[i] ||
            _playbackSpawnGenerations[i] != _session->unit_spawn_generation(index)) {
            if (unit.form > 1 || unit.facing > 3) throw g1::FormatError("Activated unit has unsupported sprite form/facing");
            const int initial = signedId(row[unit.form*4+unit.facing]);
            if (initial < 0 || static_cast<std::size_t>(initial) >= _scene->sprites[i].descriptor.animations().size())
                throw g1::FormatError("Activated unit's initial animation is not present in its descriptor");
            _animations[i].emplace(_scene->sprites[i].descriptor.animations()[initial],-1,false);
            _animationIds[i] = initial;
            _renderedActive[i] = true;
            _playbackSpawnGenerations[i] = _session->unit_spawn_generation(index);
            _playbackTransformGenerations[i] = 0;
            _pendingTransformCompletions[i].reset();
            if (index < _previousUnits.size()) _previousUnits[index] = unit;
            ++_spawnPlaybackResets;
            ++_contentGeneration;
        }
        const int special = signedId(row[8]);
        bool reverse = false;
        std::int32_t repeats = -1;
        int desired;
        if (_session->transform_animation_pending(index)) {
            desired = special;
            reverse = unit.form == 0;
            repeats = 1;
            _playbackTransformGenerations[i] = _session->unit_transform_generation(index);
            _pendingTransformCompletions[i].reset();
            _session->clear_transform_animation_request(index); // Original6cdc6.
        } else {
            if (unit.form > 1 || unit.facing > 3) throw g1::FormatError("Unsupported sprite form/facing");
            desired = signedId(row[unit.form*4+unit.facing]);
        }
        const auto beforeFrame = _animations[i]->current_frame();
        bool changed = false;
        if (_animationIds[i] == special && !_animations[i]->state().completed) {
            if (sharedDelta > std::uint32_t(std::numeric_limits<std::int32_t>::max()/2))
                throw g1::FormatError("Original shared sprite delta exceeded signed range");
            sharedDelta *= 2;
        } else if (desired != _animationIds[i] && desired != -1) {
            if (desired < 0 || static_cast<std::size_t>(desired) >= _scene->sprites[i].descriptor.animations().size())
                throw g1::FormatError("Sprite animation request exceeds recovered descriptor");
            // Original 6ce42..6ce50 clears global map+28 when the OLD
            // animation equals row8. It does not compare that global to the
            // current unit. Native host completion is deferred until the
            // resulting normal-animation frame has actually been presented.
            if (_animationIds[i] == special && _playbackTransformGenerations[i] != 0)
                _pendingTransformCompletions[i] = NativeTransformCompletion{index,
                    _playbackTransformGenerations[i],_playbackSpawnGenerations[i]};
            _animations[i] = g1::SpriteAnimation(_scene->sprites[i].descriptor.animations()[desired],repeats,reverse);
            _animationIds[i] = desired;
            changed = true; // Original new sequence is not advanced in this tick.
        }
        if (!changed) {
            if (unit.flag_2c && worldUnit.owner == _session->current_player() && _animationIds[i] != special)
                _animations[i]->select_step(0);
            else _animations[i]->advance_ms(sharedDelta);
        }
        if (changed || beforeFrame != _animations[i]->current_frame()) ++_contentGeneration;
    }
}

- (void)simulate:(std::uint32_t)delta {
    if(_typeListOpen)return;
    const auto installedBefore=_sessionInstalls;
    const auto panelBefore=_panelPurpose;
    [self updateCampaign:0];
    if(installedBefore!=_sessionInstalls || (panelBefore!=NativePanel::none && _panelPurpose==NativePanel::none))delta=0;
    if(_panelPurpose!=NativePanel::none) {
        const auto steps=_simulationClock.add_elapsed(delta);
        for(std::uint32_t n=0;n<steps && _panelPurpose!=NativePanel::none;++n)
            [self updateCampaign:g1::SimulationClock::step_ms];
        return;
    }
    [self showNextMessage];
    if(_panelPurpose!=NativePanel::none || _typeListOpen)return;
    [self synchronizeScene];
    [self prepareWorldPresentation];
    [self refreshWorldOverlays];
    [self refreshTerrainDecorations];
    const auto steps = _simulationClock.add_elapsed(delta);
    for (std::uint32_t n=0;n<steps;++n) {
        _previousUnits = _session->units();
        [self refreshControls];
        if(_panelPurpose!=NativePanel::none)break;
        if (_session->battle()) {
            if(_supportPresentation && !_supportPresentation->status().complete) {
                const auto status=_supportPresentation->status();
                // Native host policy: each cue must appear once. Subsequent
                // simulation ticks do not wait for each animation revision.
                if(_supportDisplay.can_advance(status)) {
                    const auto action=_supportPresentation->advance(g1::SimulationClock::step_ms);
                    ++_supportTicks;++_contentGeneration;
                    if(action==g1::SupportPresentationAction::start_main_battle) {
                        [self prepareBattle];break;
                    }
                    [self maintainMusicAfterStep:g1::MusicMaintenanceSite::map_update];
                    if(action==g1::SupportPresentationAction::show_next_helper) {
                        [self showSupportHelper];break;
                    }
                }
            } else if (_battlePresentation && _battlePresentedToken==_battlePresentation->status().token &&
                !_battlePresentation->status().complete) {
                _battlePresentation->advance(g1::SimulationClock::step_ms);
                for (const auto request:_battlePresentation->take_audio_requests()) [self dispatchAudio:request];
                [self maintainMusicAfterStep:g1::MusicMaintenanceSite::ordinary_battle];
                ++_battlePresentationTicks; ++_contentGeneration;
            }
            continue;
        }
        [self advanceCamera:g1::SimulationClock::step_ms];
        g1::MissionTickInput input;
        if (_session->mission().gate == g1::MissionGate::dialogue) {
            input.dialogue_layout = [_dialogueCanvas layoutFeedback];
            if (input.dialogue_layout->box_height <= 0 || input.dialogue_layout->line_height <= 0)
                continue; // No simulation/dialogue completion before real native layout.
            if (_inputCommand == _session->mission().command_index) input.input_edges = _inputEdges;
        } else _inputEdges = 0;
        const auto result = _session->tick(g1::SimulationClock::step_ms,input);
        [self synchronizeScene];
        [self prepareWorldPresentation];
        [self refreshWorldOverlays];
        [self refreshTerrainDecorations];
        _inputEdges = result.mission.remaining_input_edges;
        for(const auto request:result.audio)[self dispatchAudio:request];
        _pendingMessageIds.insert(_pendingMessageIds.end(),result.message_ids.begin(),result.message_ids.end());
        for (const auto effect : result.mission.effects)
            if (effect == g1::MissionEffect::skip_dialogue_audio_event)
                [self dispatchAudio:g1::MissionAudioRequest{5,18}];
        if (result.completion) {
            for (const auto request : result.completion->audio_requests) [self dispatchAudio:request];
            _pendingCompletionRequests.emplace_back(_session->mission().command_index,*result.completion);
            if(result.completion->text_resource_id)_pendingMessageIds.push_back(*result.completion->text_resource_id);
        }
        ++_simulationTicks;
        if (result.movement_finished || result.world_changed || result.turn_transition) {
            _rangeDirty = true;_checkpointRequested=true;
        }
        if(_session->battle()) {
            // An AI attack may have created the battle in this very tick.
            // Preserve pre-combat sprite playback and pending transforms.
            [self prepareBattle];break;
        }
        [self advanceSprites:g1::SimulationClock::step_ms];
        [self showNextMessage];
        if(_panelPurpose!=NativePanel::none)break;
        [self maintainMusicAfterStep:g1::MusicMaintenanceSite::map_update];
    }
    if(_panelPurpose!=NativePanel::none || _typeListOpen)return;
    if(!_session->battle())[self advanceCamera:0];
    [self refreshControls];
    if(_session->checkpoint_available() && _checkpointCommand!=_session->mission().command_index)
        _checkpointRequested=true;
    [self saveCheckpoint];
    const float blend = _simulationClock.interpolation();
    for (std::size_t i=0;i<_unitModels.size();++i) {
        const auto index = _scene->sprites[i].unit_index;
        const auto& unit = _session->units().at(index);
        auto model = unit_model(_session->world(),unit);
        if (index < _previousUnits.size() && _previousUnits[index].form == unit.form) {
            const auto previous = unit_model(_session->world(),_previousUnits[index]);
            for (std::size_t j=12;j<15;++j) model[j] = previous[j]+(model[j]-previous[j])*blend;
        }
        if (model != _unitModels[i]) { _unitModels[i] = model; ++_contentGeneration; }
    }
}

- (void)deliverPresentation:(std::shared_ptr<NativePresentationDelivery>)delivery {
    if (!delivery || delivery->delivered || delivery->failed || !delivery->completed ||
        delivery->presented_time<=0 || !std::isfinite(delivery->presented_time)) return;
    delivery->delivered=true;
    [self recordPresentation:delivery->presented_time frame:delivery->frame];
}

- (void)recordPresentation:(double)time frame:(std::shared_ptr<const NativePresentedFrame>)frame {
    if (!frame || frame->scene_revision != _sceneRevision ||
        !_presentationEpoch.matches(frame->presentation_epoch)) return;
    if (_failed || _metalView.paused || time <= 0 || !std::isfinite(time)) return;
    if (!_frameStats.times.empty() && time <= _frameStats.times.back()) return;
    _frameStats.add(time);
    if (frame->content_generation != _lastPresentedContent) {
        ++_presentedContentChanges; _lastPresentedContent = frame->content_generation;
    }
    if(time-_lastHudTime>=0.5 && _frameStats.times.size()>=2) {
        _lastHudTime=time;
        _fps.text=[NSString stringWithFormat:@"%.1f presented fps · target %ld",_frameStats.fps(),
            static_cast<long>(_metalView.preferredFramesPerSecond)];
    }
    if(_panelPurpose!=NativePanel::none || _typeListOpen)return;
    try {
        if(frame->support) {
            ++_supportPresentedFrames;
            if(_session->battle() && _supportPresentation &&
               _session->battle()->token==frame->support->token)
                _supportDisplay.observe(_supportPresentation->status(),*frame->support);
            // A support drawable is never a tactical or main-battle ack.
            return;
        } else if (frame->battle) {
            ++_battlePresentedFrames; _battleQueuedDrawsPresented+=frame->battle->queued_draws;
            if (_session->battle() && _battlePresentation &&
                _session->battle()->token==frame->battle->token &&
                _battlePresentation->status().token==frame->battle->token) {
                _battlePresentedToken=frame->battle->token;
                [self finishBattleFrame:*frame->battle];
            }
        } else if (_session->battle()) {
            // A queued tactical drawable cannot acknowledge hidden tactical
            // focus, transform or world work while the battle is now active.
            return;
        }
        if(frame->world && _session->world_presentation() &&
           _session->world_presentation()->token==*frame->world &&
           _preparedWorldToken && *_preparedWorldToken==*frame->world) {
            _session->report_world_presented(*frame->world);++_worldReports;
            _preparedWorldToken.reset();_rangeDirty=true;_checkpointRequested=true;
        }
        if (frame->mission_camera && _session->mission().gate == g1::MissionGate::camera &&
            _session->mission().command_index == *frame->mission_camera) {
            _session->report_camera(*frame->mission_camera,true);
            ++_cameraReports;
        }
        if (frame->session_focus && _session->focus_request() &&
            _session->focus_request()->token == *frame->session_focus) {
            _session->report_focus_presented(*frame->session_focus);
            ++_focusReports;
            _checkpointRequested=true;
        }
        for (const auto& completion : frame->transforms) {
            const auto index = completion.unit_index;
            if (index >= _session->units().size() || !_session->units()[index].active ||
                !_session->unit_transform_waiting(index) ||
                _session->unit_transform_generation(index) != completion.transform_generation ||
                _session->unit_spawn_generation(index) != completion.spawn_generation) continue;
            _session->report_transform_finished(index,completion.transform_generation);
            ++_transformReports;
            _checkpointRequested=true;
            for (auto& pending : _pendingTransformCompletions)
                if (pending && pending->unit_index == index &&
                    pending->transform_generation == completion.transform_generation &&
                    pending->spawn_generation == completion.spawn_generation) pending.reset();
        }
        [self saveCheckpoint];
    } catch (const std::exception& e) { [self fail:native_text(e.what())]; }
}

- (void)recordCommandFailure:(NSString*)message revision:(std::uint64_t)revision epoch:(std::uint64_t)epoch {
    if(revision!=_sceneRevision || !_presentationEpoch.matches(epoch)){++_staleGpuErrors;return;}
    [self fail:message];
}

- (void)drawInMTKView:(MTKView*)view {
    if (!_loaded || _failed) return;
    @autoreleasepool {
        try {
            id<CAMetalDrawable> drawable = view.currentDrawable;
            MTLRenderPassDescriptor* pass = view.currentRenderPassDescriptor;
            if (!drawable || !pass) return;
            g1::quality::Capability capabilities;
            capabilities.memory_bytes=NSProcessInfo.processInfo.physicalMemory;
            capabilities.low_power=NSProcessInfo.processInfo.lowPowerModeEnabled;
            capabilities.thermal_serious=NSProcessInfo.processInfo.thermalState>=NSProcessInfoThermalStateSerious;
            capabilities.memory_pressure=_renderMemoryPressure;
            _renderQuality=g1::quality::plan(static_cast<std::uint32_t>(view.drawableSize.width),
                static_cast<std::uint32_t>(view.drawableSize.height),
                _graphicsModern?g1::quality::Mode::modern:g1::quality::Mode::classic,capabilities);
            pass=_supersampling->begin(pass,_renderQuality);
            const CGSize renderSize=_supersampling->active()?CGSizeMake(_renderQuality.width,_renderQuality.height):view.drawableSize;
            const double now = CACurrentMediaTime();
            if (!_clockStarted) { _clockOrigin = now; _lastTick = 0; _clockStarted = true; }
            const auto tick = static_cast<std::uint32_t>(
                static_cast<std::uint64_t>((now - _clockOrigin) * 1000.0));
            // Original core uses integer milliseconds and a default 1000ms
            // maximum delta. Display refresh is never used as a game tick.
            const auto delta = std::min(std::uint32_t(tick - _lastTick),std::uint32_t(1000));
            if (std::uint32_t(tick - _lastTick) > 1000U) ++_clampedClockIntervals;
            _lastTick = tick;
            [self simulate:delta];
            if(!_loaded || !_scene)return;
            id<MTLCommandBuffer> command = [_queue commandBuffer];
            if (!command) throw g1::FormatError("Metal command buffer is unavailable");
            auto frame = std::make_shared<NativePresentedFrame>();
            frame->content_generation = _contentGeneration;
            frame->scene_revision = _sceneRevision;
            if(_session->battle())[self prepareBattle];
            frame->presentation_epoch = _presentationEpoch.current();
            const bool support=_session->battle() && _supportPresentation &&
                !_supportPresentation->status().complete;
            if (_session->battle() && !support) {
                std::vector<const g1::M3GRenderAsset*> uploaded;
                for (const auto& model:_battleModels) uploaded.push_back(&model->asset());
                const auto size=renderSize;
                // Native XR policy: preserve authored vertical FOV/parallel
                // height, changing only aspect for the actual drawable.
                const auto output=_battlePresentation->prepare_frame(
                    {{static_cast<float>(size.width),static_cast<float>(size.height)}},uploaded,
                    _simulationClock.interpolation(),static_cast<float>(size.width/size.height));
                const auto damageStatus=_battlePresentation->status();
                if(damageStatus.damage_text) {
                    const std::size_t target=damageStatus.damage_on_left?1U:2U;
                    for(const auto& submission:output.submissions)if(submission.asset_index==target) {
                        if(const auto anchor=g1::projected_actor_top(submission.frame)) {
                            const CGPoint rootPoint=CGPointMake((*anchor)[0]*view.bounds.size.width,(*anchor)[1]*view.bounds.size.height);
                            const CGPoint p=[view convertPoint:rootPoint toView:_battleCanvas];
                            if(_battleCanvas.bounds.size.width>0 && _battleCanvas.bounds.size.height>0)
                                [_battleCanvas setDamageAnchor:CGPointMake(p.x/_battleCanvas.bounds.size.width,p.y/_battleCanvas.bounds.size.height)
                                    token:output.token revision:output.revision];
                        }
                    }
                }
                pass.colorAttachments[0].loadAction=output.clear_color ? MTLLoadActionClear : MTLLoadActionLoad;
                pass.colorAttachments[0].clearColor=MTLClearColorMake(output.background_color[0],
                    output.background_color[1],output.background_color[2],output.background_color[3]);
                pass.depthAttachment.loadAction=output.clear_depth ? MTLLoadActionClear : MTLLoadActionLoad;
                pass.depthAttachment.clearDepth=1;
                _supersampling->sanitize_new_target(pass);
                id<MTLRenderCommandEncoder> encoder=[command renderCommandEncoderWithDescriptor:pass];
                if (!encoder) throw g1::FormatError("Metal battle render encoder is unavailable");
                [encoder setViewport:MTLViewport{0,0,size.width,size.height,0,1}];
                std::size_t queued=0;
                for (const auto& submission:output.submissions) {
                    // Each original node call flushes its own queue. World
                    // clear occurs once; A/B/effects retain this order/depth.
                    _m3gRenderer->encode_frame(encoder,{_battleModels.at(submission.asset_index)},submission.frame);
                    queued+=submission.frame.draws.size();
                }
                [encoder endEncoding];
                frame->battle=NativePresentedFrame::Battle{output.token,output.revision,output.complete,queued};
                ++_battlePreparedFrames;
            } else {
                pass.colorAttachments[0].loadAction=MTLLoadActionClear;
                pass.colorAttachments[0].clearColor=view.clearColor;
                pass.depthAttachment.loadAction=MTLLoadActionClear;
                pass.depthAttachment.clearDepth=1;
                const auto camera = _camera.matrix();
                _tacticalDrawn=0;_tacticalFallback=0;_tacticalHitBounds.clear();
                std::vector<std::pair<std::shared_ptr<g1::MetalM3GModel>,g1::M3GPreparedFrame>> tacticalFrames;
                std::vector<g1::MetalDraw> draws{
                    {_terrain,[self spriteTexture:_scene->terrain.atlas_resource_id],camera,{{1,1,1,1}},true}};
                if(_scene->terrain.animated_surface && !_session->terrain_state())
                    throw g1::FormatError("Animated terrain has no native clock state");
                const float terrainOffset=_session->terrain_state() ?
                    g1::terrain_animation_u_offset(_session->terrain_state()->animation) : 0;
                // Original420 indices reserve transient slots before fixed faces.
                if(_decorationTerrain && _scene->terrain.animated_surface)
                    draws.push_back({_decorationTerrain,_textures[@(_scene->terrain.animated_surface->atlas_resource_id)],
                        camera,{{1,1,1,1}},true,{{terrainOffset,0}}});
                if(_animatedTerrain && _scene->terrain.animated_surface)
                    draws.push_back({_animatedTerrain,_textures[@(_scene->terrain.animated_surface->atlas_resource_id)],
                        camera,{{1,1,1,1}},true,{{terrainOffset,0}}});
                // Recovered visible/attached nodes and sibling order. The tactical
                // inspection renderer's grouping against unit overlays is native
                // presentation policy, not a claim of the full original draw tree.
                for(const auto& decoration:_terrainSpriteDraws)
                    draws.push_back({_terrainSpriteMeshes.at(decoration.resource.descriptor_id).at(decoration.frame_index),
                        [self spriteTexture:decoration.resource.image_id],
                        g1::multiply_matrix(camera,decoration.model),{{1,1,1,1}},true,{},false,false});
                for(const auto& overlay:_worldOverlays)
                    draws.push_back({overlay.mesh,_white,camera,overlay.tint,false});
                if(!support) {
                    if (_range) draws.push_back({_range,_white,camera,{{0.12F,0.9F,0.85F,0.36F}},false});
                    if (_cursor) draws.push_back({_cursor,_white,camera,{{1,0.82F,0.12F,0.6F}},false});
                    if (_attackRange) draws.push_back({_attackRange,_white,camera,{{1,0.18F,0.14F,0.48F}},false});
                    if(_healRange)draws.push_back({_healRange,_white,camera,{{0.20F,1,0.35F,0.48F}},false});
                    if(_productionRange)draws.push_back({_productionRange,_white,camera,{{0.25F,0.85F,1,0.48F}},false});
                } else if(_supportCursor)
                    draws.push_back({_supportCursor,_white,camera,{{1,0.82F,0.12F,0.6F}},false});
                std::vector<std::size_t> order;
                if(support) {
                    // Freeze ordinary tactical playback in its pre-combat
                    // pose; only the separately recovered effect advances.
                    auto snapshots=_supportUnitDraws;
                    for(auto& draw:snapshots)draw.matrix=g1::multiply_matrix(camera,draw.matrix);
                    std::stable_sort(snapshots.begin(),snapshots.end(),[](const auto& a,const auto& b) {
                        return transform(a.matrix,{{0,0,0,1}})[2]>transform(b.matrix,{{0,0,0,1}})[2];
                    });
                    draws.insert(draws.end(),snapshots.begin(),snapshots.end());
                    const auto status=_supportPresentation->status();
                    if(!status.effect_complete) {
                        const auto& unit=_session->battle()->units_before.at(status.helper_unit.value());
                        const auto size=renderSize;
                        const auto vertices=g1::support_overlay_vertices(_supportAssets->descriptor,status.effect_frame,
                            _supportAssets->image.width,_supportAssets->image.height,camera,unit.tile_x,unit.tile_y,
                            static_cast<float>(size.width),static_cast<float>(size.height));
                        if(!vertices.empty())draws.push_back({_renderer->mesh(vertices),_supportTexture,
                            g1::identity_matrix(),{{1,1,1,1}},false,{},true,true});
                    }
                    frame->support=_supportDisplay.receipt(status,bool([_title currentTextDrawn]));
                } else {
                for (std::size_t i = 0; i < _animations.size(); ++i)
                    if (_session->units().at(_scene->sprites[i].unit_index).active && _animations[i]) order.push_back(i);
                std::stable_sort(order.begin(),order.end(),[&](std::size_t a,std::size_t b) {
                    const auto pa = transform(g1::multiply_matrix(camera,_unitModels[a]),{{0,0,0,1}});
                    const auto pb = transform(g1::multiply_matrix(camera,_unitModels[b]),{{0,0,0,1}});
                    return pa[2] > pb[2];
                });
                if(_renderQuality.soft_contact_shadows && !support && _contactShadowMesh) {
                    for(const auto i:order) {
                        const auto& unit=_session->units().at(_scene->sprites[i].unit_index);
                        if(!unit.active || unit.tile_x>=_session->world().grid.width ||
                            unit.tile_y>=_session->world().grid.height)continue;
                        const auto cell=_session->world().grid.tile_at(unit.tile_x,unit.tile_y);
                        if(cell>=32)continue;
                        auto model=g1::identity_matrix();
                        model[12]=_unitModels[i][12];model[14]=_unitModels[i][14];
                        model[13]=_terrainTables.at(_session->world().style).at(cell).height_steps*0.32F+0.002F;
                        draws.push_back({_contactShadowMesh,_contactShadowTexture,
                            g1::multiply_matrix(camera,model),{{1,1,1,1}},false});
                    }
                }
                for (const auto i : order) {
                    const auto index=_scene->sprites[i].unit_index;
                    const auto& unit=_session->units().at(index);
                    const auto type=_session->world().units.at(index).type;
                    const bool moving=unit.offset_x!=0 || unit.offset_y!=0 ||
                        (_session->moving() && (!_session->selected() || *_session->selected()==index));
                    const bool transforming=_session->unit_transform_waiting(index) ||
                        _session->transform_animation_pending(index)!=0;
                    bool replaced=false;
                    if(_graphicsModern && unit.active && !support && !_renderMemoryPressure && !_tacticalErrors.count(type)) {
                        try {
                            if(!_tacticalModels.count(type)) {
                                auto model=std::make_unique<g1::TacticalModel>(*_archive,type);
                                auto gpu=_m3gRenderer->upload_modern(model->asset(),g1::MaterialEnhancement::character);
                                _tacticalGpu.emplace(type,std::move(gpu));_tacticalModels.emplace(type,std::move(model));
                            }
                            const auto& model=*_tacticalModels.at(type);
                            const auto& world=_session->world();
                            const auto cell=world.grid.tile_at(unit.tile_x,unit.tile_y);
                            if(cell<32 && model.supports(unit.form) &&
                               (!moving || transforming || model.walking_supported(unit.form)) &&
                               (!transforming || (model.supports(0)&&model.supports(1)))) {
                                const float height=_terrainTables.at(world.style).at(cell).height_steps*0.32F;
                                // Same simulation interpolation as the original sprites, but
                                // a real tile-centred foot instead of the sprite billboard offset.
                                float x=(unit.tile_x+0.5F)*0.32F+unit.offset_x*0.01F;
                                float z=(unit.tile_y+0.5F)*0.32F+unit.offset_y*0.01F;
                                if(index<_previousUnits.size() && _previousUnits[index].active &&
                                   _previousUnits[index].form==unit.form) {
                                    const auto& previous=_previousUnits[index];
                                    const float px=(previous.tile_x+0.5F)*0.32F+previous.offset_x*0.01F;
                                    const float pz=(previous.tile_y+0.5F)*0.32F+previous.offset_y*0.01F;
                                    if(std::abs(x-px)<=0.64F && std::abs(z-pz)<=0.64F) {
                                        const float a=_simulationClock.interpolation();
                                        x=px+(x-px)*a;z=pz+(z-pz)*a;
                                    }
                                }
                                g1::TacticalMotion motion;motion.walking=moving&&!transforming;
                                motion.transforming=transforming;
                                motion.stride_phase=g1::tactical_stride(x,z,unit.facing);
                                motion.vehicle_fraction=unit.form;
                                if(transforming) {
                                    const auto raw=_animationTable.at(type)[8];
                                    const int special=raw<128?int(raw):int(raw)-256;
                                    if(_animationIds[i]==special && special>=0)
                                        motion.vehicle_fraction=g1::transformation_fraction(
                                            _scene->sprites[i].descriptor.animations().at(special),_animations[i]->state());
                                    else if(_session->transform_animation_pending(index))
                                        motion.vehicle_fraction=unit.form==0?1.0F:0.0F;
                                    // Otherwise the normal target frame is being presented;
                                    // original generation/receipt logic still owns completion.
                                }
                                auto gpu=_tacticalGpu.at(type);
                                auto prepared=model.prepare(gpu->asset(),unit.form,unit.facing,
                                    {{x,height+0.002F,z}},camera,
                                    {{static_cast<std::uint32_t>(renderSize.width),static_cast<std::uint32_t>(renderSize.height)}},motion);
                                CGRect hit=CGRectNull;
                                for(const auto& draw:prepared.draws)for(const auto& v:gpu->asset().geometry.surfaces.at(draw.geometry_index).vertices) {
                                    const auto p=transform(draw.metal_matrix,v.position);
                                    if(p[3]<=0.0001F || p[2]<0 || p[2]>p[3])continue;
                                    const double px=(p[0]/p[3]*0.5+0.5)*view.bounds.size.width;
                                    const double py=(0.5-p[1]/p[3]*0.5)*view.bounds.size.height;
                                    hit=CGRectUnion(hit,CGRectMake(px,py,0.1,0.1));
                                }
                                if(!prepared.draws.empty()) {
                                    if(!CGRectIsNull(hit))_tacticalHitBounds[i]=hit;
                                    tacticalFrames.emplace_back(std::move(gpu),std::move(prepared));replaced=true;++_tacticalDrawn;
                                }
                            }
                        } catch(const std::exception& error) {
                            // Source sprite remains present on a model-specific
                            // decode/allocation failure. Record it, never hide unit.
                            _tacticalErrors[type]=error.what();
                        }
                    }
                    if(!replaced) {
                        ++_tacticalFallback;
                        const auto texture = [self spriteTexture:_scene->sprites[i].resource.image_id];
                        draws.push_back({_spriteMeshes[i][_animations[i]->current_frame()],texture,
                            g1::multiply_matrix(camera,_unitModels[i]),{{1,1,1,1}},true,{},false,false});
                    }
                }
                }
                id<MTLRenderCommandEncoder> tacticalEncoder=[command renderCommandEncoderWithDescriptor:pass];
                if(!tacticalEncoder)throw g1::FormatError("Tactical shared encoder unavailable");
                try {
                    _renderer->encode_into(tacticalEncoder,draws);
                    for(const auto& item:tacticalFrames)_m3gRenderer->encode_frame(tacticalEncoder,{item.first},item.second);
                } catch(...) {[tacticalEncoder endEncoding];throw;}
                [tacticalEncoder endEncoding];
                if(!support && _panelPurpose==NativePanel::none && _preparedWorldToken && _session->world_presentation() &&
                   _session->world_presentation()->token==*_preparedWorldToken && [_title currentTextDrawn]) {
                    bool rosterReady=true;
                    for(std::size_t i=0;i<_scene->sprites.size();++i) {
                        const auto index=_scene->sprites[i].unit_index;
                        if(_session->units().at(index).active &&
                           (!_animations[i] || !_renderedActive[i] ||
                            _playbackSpawnGenerations[i]!=_session->unit_spawn_generation(index)))rosterReady=false;
                    }
                    if(rosterReady)frame->world=*_preparedWorldToken;
                }
                if (!support && _cameraPan && _cameraPan->reached) {
                    if (_cameraPan->session_focus && _session->focus_request() &&
                        _session->focus_request()->token == _cameraPan->token)
                        frame->session_focus = _cameraPan->token;
                    else if (!_cameraPan->session_focus && _session->mission().gate == g1::MissionGate::camera &&
                        _session->mission().command_index == _cameraPan->token)
                        frame->mission_camera = static_cast<std::size_t>(_cameraPan->token);
                }
                if(!support)for (const auto i : order) {
                    const auto& pending = _pendingTransformCompletions[i];
                    if (pending && _session->unit_transform_waiting(pending->unit_index) &&
                        _session->unit_transform_generation(pending->unit_index) == pending->transform_generation &&
                        _session->unit_spawn_generation(pending->unit_index) == pending->spawn_generation)
                        frame->transforms.push_back(*pending);
                }
            }
            _supersampling->resolve(command,drawable.texture);
            __weak G1ViewController* weakSelf = self;
            const std::shared_ptr<const NativePresentedFrame> presentedFrame = frame;
            auto delivery=std::make_shared<NativePresentationDelivery>();
            delivery->frame=presentedFrame;
            [drawable addPresentedHandler:^(id<MTLDrawable> presented) {
                const double time = presented.presentedTime;
                dispatch_async(dispatch_get_main_queue(),^{
                    delivery->presented_time=time;
                    [weakSelf deliverPresentation:delivery];
                });
            }];
            [command addCompletedHandler:^(id<MTLCommandBuffer> completed) {
                if (completed.status == MTLCommandBufferStatusError) {
                    NSString* reason = completed.error.localizedDescription ?: @"Metal command failed";
                    dispatch_async(dispatch_get_main_queue(),^{
                        delivery->failed=true;
                        [weakSelf recordCommandFailure:reason revision:presentedFrame->scene_revision
                            epoch:presentedFrame->presentation_epoch];
                    });
                } else if (completed.status == MTLCommandBufferStatusCompleted) {
                    dispatch_async(dispatch_get_main_queue(),^{
                        delivery->completed=true;
                        [weakSelf deliverPresentation:delivery];
                    });
                }
            }];
            [command presentDrawable:drawable];
            [command commit];
        } catch (const std::exception& error) {
            [self fail:[NSString stringWithUTF8String:error.what()]];
        }
    }
}

- (void)shareReport {
    if(self.presentedViewController)return;
    struct utsname system{};
    uname(&system);
    NSDictionary* info = NSBundle.mainBundle.infoDictionary;
    NSMutableArray* unitState = [NSMutableArray new];
    NSMutableArray* siteState = [NSMutableArray new];
    NSMutableArray* completionRequests = [NSMutableArray new];
    NSMutableArray* battleHistory = [NSMutableArray new];
    NSMutableArray* battleAssets = [NSMutableArray new];
    if (_battlePresentation)
        for (const auto& asset:_battlePresentation->assets()) [battleAssets addObject:@(asset.resource_id)];
    NSMutableArray* messageHistory=[NSMutableArray new];
    for(const auto identifier:_presentedMessageIds)[messageHistory addObject:@(identifier)];
    const auto battleJson = [](const g1::SessionBattle& battle) -> NSDictionary* {
        const auto strikeJson = [](const g1::CombatStrike& strike) -> NSDictionary* {
            return @{@"rolled_damage":@(strike.rolled_damage),@"health_before":@(strike.health_before),
                @"health_after":@(strike.health_after),@"evaded":@(strike.evaded),@"immune":@(strike.immune),
                @"random_words":@(strike.random_words)};
        };
        NSMutableArray* destroyed = [NSMutableArray new];
        for (const auto& unit : battle.exchange.destroyed)
            [destroyed addObject:@{@"type":@(unit.type),@"owner":@(unit.owner),@"x":@(unit.x),@"y":@(unit.y)}];
        NSMutableArray* support = [NSMutableArray new];
        for (const auto type : battle.exchange.support_types) [support addObject:@(type)];
        return @{@"token":@(battle.token),@"attacker_index":@(battle.attacker),@"defender_index":@(battle.defender),
            @"attacker_type":@(battle.attacker_before.type),@"defender_type":@(battle.defender_before.type),
            @"attacker_health_before":@(battle.attacker_before.health),@"defender_health_before":@(battle.defender_before.health),
            @"attack":strikeJson(battle.exchange.attack),
            @"counterattack":battle.exchange.counterattack ? strikeJson(*battle.exchange.counterattack) : NSNull.null,
            @"destroyed":destroyed,@"support_types":support,@"random_words":@(battle.exchange.random_words)};
    };
    for (const auto& battle : _completedBattles) [battleHistory addObject:battleJson(battle)];
    for (const auto& entry : _pendingCompletionRequests) {
        NSMutableArray* audio = [NSMutableArray new];
        for (const auto& request : entry.second.audio_requests)
            [audio addObject:@{@"operation":@(request.operation),@"argument":@(request.argument)}];
        [completionRequests addObject:@{@"command_index":@(entry.first),@"audio":audio,
            @"text_resource_id":entry.second.text_resource_id ? @(*entry.second.text_resource_id) : NSNull.null,
            @"audio_requests_forwarded_to_host":@(_audio != nil),
            @"text_request_forwarded_to_native_queue":@(entry.second.text_resource_id.has_value())}];
    }
    if (_session) {
        for (std::size_t i=0;i<_session->units().size();++i) {
            const auto& unit = _session->units()[i];
            const auto& world = _session->world().units[i];
            const auto& combat = _session->combat_units()[i];
            [unitState addObject:@{@"type":@(world.type),@"owner":@(world.owner),@"form":@(unit.form),
                @"active":@(unit.active),@"x":@(unit.tile_x),@"y":@(unit.tile_y),
                @"offset_x":@(unit.offset_x),@"offset_y":@(unit.offset_y),@"facing":@(unit.facing),
                @"spent_movement":@(unit.spent_budget),@"action_used_flag2c":@(unit.flag_2c),
                @"transformed_flag2d":@(unit.flag_2d),@"health":@(combat.health),
                @"spawn_generation":@(_session->unit_spawn_generation(i)),
                @"transform_generation":@(_session->unit_transform_generation(i)),
                @"transform_waiting_for_presentation":@(_session->unit_transform_waiting(i))}];
        }
        for (const auto& site : _session->sites()) {
            NSMutableArray* groups = [NSMutableArray new];
            for (const auto& group : site.item_states) {
                NSMutableArray* items = [NSMutableArray new];
                for (const auto owner : group) [items addObject:@(owner)];
                [groups addObject:items];
            }
            [siteState addObject:@{@"site_owner":@(site.state),@"item_owners":groups}];
        }
    }
    NSDictionary* report = @{
        @"artifact_kind": @"native campaign integration test", @"source_commit": info[@"G1SourceCommit"] ?: @"unknown",
        @"app_version": info[@"CFBundleShortVersionString"] ?: @"unknown",
        @"device": [NSString stringWithUTF8String:system.machine], @"ios": UIDevice.currentDevice.systemVersion,
        @"native_scene_loaded": @(_loaded), @"failure": _failure ?: NSNull.null,
        @"scene_revision":@(_sceneRevision),@"scene_rebuilds":@(_sceneRebuilds),
        @"presentation_epoch":@(_presentationEpoch.current()),
        @"session_installs":@(_sessionInstalls),@"spawn_playback_resets":@(_spawnPlaybackResets),
        @"scene_sprite_asset_count":@(_scene ? _scene->sprites.size() : 0),
        @"scene_grid_resource":@(_scene ? _scene->world.grid_resource_id : 0),
        @"scene_style":@(_scene ? _scene->world.style : 0),
        @"fixed_animated_terrain_quads":@(_scene && _scene->terrain.animated_surface ? _scene->terrain.animated_surface->quads.size() : 0),
        @"transient_terrain_quads":@(_decorationTerrain ? _decorationTerrain->count/6 : 0),
        @"terrain_generation":@(_session ? _session->terrain_generation() : 0),
        @"terrain_clock_owner":@"GameSession; one shared random generator; host only reads geometry/UV phase",
        @"world_presentation_acknowledgements":@(_worldReports),
        @"world_presentation_pending":@(_session && _session->world_presentation().has_value()),
        @"native_world_overlay_count":@(_worldOverlays.size()),
        @"world_overlay_scope":@"Real site/item coordinates and CaptureNetwork ownership/link state, blocking site points and mission markers; native geometry, not original artwork",
        @"archive_sha256": [NSString stringWithUTF8String:archive_sha],
        @"terrain_tables_sha256": [NSString stringWithUTF8String:tables_sha],
        @"fps_requested": @(_metalView.preferredFramesPerSecond),
        @"drawable_width_pixels": @(_metalView.drawableSize.width),
        @"drawable_height_pixels": @(_metalView.drawableSize.height),
        @"screen_native_scale": @((self.view.window.screen ?: UIScreen.mainScreen).nativeScale),
        @"presented_frames_total": @(_frameStats.total),
        @"presented_interval_samples": @(_frameStats.times.size() > 0 ? _frameStats.times.size() - 1 : 0),
        @"presented_fps_window": @(_frameStats.fps()),
        @"presented_interval_median_ms": @(_frameStats.interval_percentile(0.5)),
        @"presented_interval_p95_ms": @(_frameStats.interval_percentile(0.95)),
        @"camera": @"Native inspection projection; real tile-focus requests with 360 ms native pan; original camera fidelity unverified",
        @"camera_requests_acknowledged_after_presentation": @(_cameraReports),
        @"session_focus_acknowledged_after_presentation":@(_focusReports),
        @"transform_completion_acknowledged_after_presentation":@(_transformReports),
        @"stale_previous_scene_gpu_errors":@(_staleGpuErrors),
        @"focus_pending":_session && _session->focus_request() ? @{
            @"token":@(_session->focus_request()->token),
            @"x":@(_session->focus_request()->tile.x),@"y":@(_session->focus_request()->tile.y),
            @"reason":@(static_cast<int>(_session->focus_request()->reason))} : NSNull.null,
        @"campaign_cursor": @(_session ? _session->mission().command_index : 0),
        @"mission_gate": @(_session ? static_cast<int>(_session->mission().gate) : -1),
        @"mission_stop_detail": _session ? native_text(_session->mission().stop_detail) : @"not loaded",
        @"current_player": @(_session ? _session->current_player() : 0),
        @"campaign_group":@(_session ? _session->campaign_context().group : 0),
        @"campaign_mission_index":@(_session ? _session->campaign_context().mission_index : 0),
        @"campaign_controller_gate":@(_campaign ? static_cast<int>(_campaign->state().gate) : -1),
        @"campaign_cycle_profile_persisted":@(_campaign && _campaign->state().profile_persisted),
        @"campaign_results_drawn_acknowledgements":@(_resultsReports),
        @"campaign_ending_drawn_acknowledgements":@(_endingReports),
        @"campaign_error":_campaignError ?: NSNull.null,
        @"campaign_profile_save_error":_campaign && !_campaign->state().save_error.empty() ?
            native_text(_campaign->state().save_error) : NSNull.null,
        @"mode_menu_policy":@"Original Story/Showdown/Arena definitions, unlocks and results; native G1-art menu",
        @"graphics_mode":_graphicsModern?@"Modern":@"Classic",
        @"ui_enhancement_shared":@YES,
        @"adaptive_viewport_points":@[@(self.view.bounds.size.width),@(self.view.bounds.size.height)],
        @"adaptive_safe_insets":@[@(self.view.safeAreaInsets.top),@(self.view.safeAreaInsets.left),
                                  @(self.view.safeAreaInsets.bottom),@(self.view.safeAreaInsets.right)],
        @"device_family":@(UIDevice.currentDevice.userInterfaceIdiom),
        @"supersampling_enabled":@(_supersampling && _supersampling->active()),
        @"scene_render_pixels":@[@(_renderQuality.width),@(_renderQuality.height)],
        @"supersampling_allocation_fallbacks":@(_supersampling?_supersampling->allocation_fallbacks():0),
        @"new_native_intro_file":@"G1OpeningXR.mp4; AVPlayer; user film; tap-to-skip",
        @"native_intro_active":@(_openingVisible),
        @"touch_camera_sensitivity":@(_touchCameraSpeed),
        @"installed_icon_source":@"Original user IPA iTunesArtwork, 512x512, PNG derivatives packaged outside source control",
        @"modern_sprite_texture_cache_count":@(_modernTextures.count),
        @"modern_tactical_models_uploaded":@(_tacticalGpu.size()),
        @"modern_tactical_models_drawn":@(_tacticalDrawn),
        @"modern_tactical_sprite_fallbacks":@(_tacticalFallback),
        @"modern_tactical_model_errors":@(_tacticalErrors.size()),
        @"modern_battle_enhanced_textures":@(_battleEnhancedTextures),
        @"diagnostics_enabled":@(_showDiagnostics),
        @"branding_policy":@"Original G1 menu background resource1002, iammrmikeman fan port credits; no Glu splash/blog/feedback screen; original archive retained byte-for-byte",
        @"simulation_step_ms": @(g1::SimulationClock::step_ms),
        @"simulation_ticks_total": @(_simulationTicks),
        @"clock_clamp_intervals_over1000ms": @(_clampedClockIntervals),
        @"movement_interpolation": @"Previous/current exact model translation; no extra simulation steps at display rate",
        @"presented_content_generation_changes": @(_presentedContentChanges),
        @"content_change_scope": @"Scene state/mesh generation changes; not a framebuffer pixel comparison or 60 Hz animation claim",
        @"dialogue_layout": @"Native TextKit line fragments and glyph drawing; original text/headings, native font metrics",
        @"dialogue_drawn_revision": @([_dialogueCanvas drawnRevision]),
        @"dialogue_input_masks": @[@1,@16,@128],
        @"move_actions_started": @(_moveActions),
        @"capture_actions_completed": @(_captureActions),
        @"transform_actions_started": @(_transformActions),
        @"attack_actions_started": @(_attackActions),
        @"repair_actions_completed":@(_healActions),@"upgrade_actions_completed":@(_upgradeActions),
        @"production_actions_completed":@(_productionActions),@"undo_actions_completed":@(_undoActions),
        @"end_turn_actions_completed":@(_endTurnActions),
        @"production_confirmation":@"Original direct touch type-list branch; no invented affordability filter or cargo inventory",
        @"friendly_destination_policy":@"Reachable occupied taps reach the core's original carrier admission; no UI blanket occupancy rejection",
        @"battle_presentations_acknowledged": @(_battleAcknowledgements),
        @"battle_presentation_ticks": @(_battlePresentationTicks),
        @"support_sequences_started":@(_supportSequences),
        @"support_simulation_ticks":@(_supportTicks),
        @"support_frames_presented":@(_supportPresentedFrames),
        @"support_active":@(_supportPresentation && !_supportPresentation->status().complete),
        @"support_helper_slot":@(_supportPresentation ? int(_supportPresentation->status().slot) : -1),
        @"support_tutorial_requests":@(_supportTutorialRequests),
        @"support_tutorial_pending":_campaign ? @(_campaign->profile().support_tutorial_pending) : NSNull.null,
        @"support_tutorial_save_retry_required":@(_supportTutorialRetryRequired),
        @"original_title_font_slot":@([_title originalFontSlot]),
        @"campaign_original_heading_font_slot":@([_campaignCanvas originalHeadingFontSlot]),
        @"original_bitmap_panel_policy":@"Campaign/results headings use original slot0 or slot2 and combat names use slot2 only when a complete single-line glyph run fits; otherwise readable system text. Original palette retained; UIKit body and HUD layout are native.",
        @"title_font_policy":@"Original keyed bitmap glyphs and measured spacing for supported single-line titles/support banners; native17-point nominal height and antialiased sampling; existing UIKit fallback for uncovered text. Dialogue layout remains TextKit",
        @"native_profile_format":@2,
        @"support_presentation":@"Original helper order, primary-name banner and keyed percentage artwork; separate effect ends at1680ms, cue hands off at2010ms; at most five helpers displayed without reducing six-helper combat",
        @"support_native_policy":@"320-unit logical height scaled to native drawable; wider horizontal field; nearest keyed bitmap sampling, inspection-camera focus and frozen pre-combat unit poses; first-present gate per helper. Original one-time tutorial predicate/text, with native atomic profile persistence and drawn-message acknowledgement. Original camera/GPU parity remains unverified",
        @"battle_metal_frames_prepared": @(_battlePreparedFrames),
        @"battle_metal_frames_presented": @(_battlePresentedFrames),
        @"battle_queued_draws_in_presented_frames": @(_battleQueuedDrawsPresented),
        @"battle_original_asset_ids": battleAssets,
        @"battle_controller_phase": @(_battlePresentation ? int(_battlePresentation->status().phase) : -1),
        @"battle_controller_revision": @(_battlePresentation ? _battlePresentation->status().revision : 0),
        @"battle_history":battleHistory,
        @"battle_pending":_session && _session->battle() ? battleJson(*_session->battle()) : NSNull.null,
        @"last_battle_final_presented_token":@(_lastBattleFinalToken),
        @"last_battle_final_presented_revision":@(_lastBattleFinalRevision),
        @"battle_presentation":@"Original M3G models, forms, texture overrides, anchors, clips, cameras, shot/hit/death effects and ordered node passes; native point HUD uses actual controller health. Natural completion requires matching final Metal presentation and drawn HUD revision",
        @"battle_aspect_policy":@"Preserve original vertical FOV or parallel height; adapt only aspect to native drawable width/height",
        @"battle_interpolation":@"Read-only integer-millisecond animation resampling between original 30ms controller states; additive camera translations interpolate; camera/clip/placement cuts remain discrete. No RNG, audio, phase or completion advance at display rate",
        @"audio_game_requests_observed": @(_audioRequests), @"audio_playback_implemented": @YES,
        @"audio_host":_audio ? [_audio diagnostics] : @{@"operational":@NO,@"error":@"Audio host was not created"},
        @"completion_presentation_requests":completionRequests,
        @"original_message_ids_drawn_and_acknowledged":messageHistory,
        @"native_checkpoint_writes_confirmed":@(_checkpointWrites),
        @"native_checkpoint_installs":@(_checkpointLoads),
        @"native_checkpoint_retirements":@(_checkpointRetirements),
        @"current_session_restored_from_checkpoint":@(_restoredCheckpoint),
        @"checkpoint_available_now":@(_session && _session->checkpoint_available()),
        @"checkpoint_retry_required":@(_checkpointRetryRequired),
        @"checkpoint_error":_checkpointError ?: NSNull.null,
        @"checkpoint_policy":@"Atomic completed-action/objective saves in Application Support; busy actions retain last checkpoint; corrupt files are preserved; successful handoffs retire prior Continue to a retained backup",
        @"random_seed": @(_randomSeed), @"units":unitState, @"sites":siteState,
        @"full_gameplay_implemented": @NO, @"full_campaign_verified": @NO,
        @"native_save_reload_verified": @NO,
        @"remaining_scope": @"Physical iPhone XR/iOS17.5 playthrough, tutorial/lifecycle and battle/support GPU output fidelity and sustained60Hz validation; site/marker art, tactical camera and HUD/font fidelity, complete sound-event coverage and device audibility",
        @"measurement_scope": @"Positive monotonic Metal presentedTime samples from current native scene; not full-game performance"
    };
    NSError* error = nil;
    NSData* json = [NSJSONSerialization dataWithJSONObject:report
        options:NSJSONWritingPrettyPrinted | NSJSONWritingSortedKeys error:&error];
    NSURL* file = [NSURL fileURLWithPath:[NSTemporaryDirectory()
        stringByAppendingPathComponent:@"G1-Native-Gameplay-Report.json"]];
    if (!json || ![json writeToURL:file options:NSDataWritingAtomic error:&error]) {
        _hint.text = error.localizedDescription ?: @"Could not save the test report";
        return;
    }
    UIActivityViewController* sheet = [[UIActivityViewController alloc]
        initWithActivityItems:@[file] applicationActivities:nil];
    // A share sheet hides gameplay just like Pause. Keep a pre-existing
    // message/menu intact; active gameplay requires an explicit Resume when
    // the sheet closes, so battles cannot finish unseen behind diagnostics.
    if(_session && !_failed && _panelPurpose==NativePanel::none)[self openMenu];
    sheet.popoverPresentationController.sourceView = _reportButton;
    sheet.popoverPresentationController.sourceRect = _reportButton.bounds;
    [self presentViewController:sheet animated:YES completion:nil];
}
@end

@interface G1AppDelegate : UIResponder <UIApplicationDelegate>
@property(nonatomic,strong) UIWindow* window;
@end
@implementation G1AppDelegate
- (BOOL)application:(UIApplication*)application didFinishLaunchingWithOptions:(NSDictionary*)options {
    (void)application; (void)options;
    self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
    self.window.rootViewController = [G1ViewController new];
    [self.window makeKeyAndVisible];
    return YES;
}
@end

int main(int argc, char** argv) {
    @autoreleasepool { return UIApplicationMain(argc,argv,nil,NSStringFromClass(G1AppDelegate.class)); }
}
