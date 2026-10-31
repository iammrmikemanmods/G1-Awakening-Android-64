#!/usr/bin/env python3
"""Record unchanged original tactical-support entry and update instructions.

Prepared helper slots, their names/coordinates, and the saved cursor are
explicit inputs. UI/string/effect/camera endpoints and the main-combat handoff
are recorded boundaries. No combat, RNG, GPU, or full-game claim is made.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys


def sha(data):return hashlib.sha256(data).hexdigest()
def signed(raw):return struct.unpack('<i',struct.pack('<I',raw&0xffffffff))[0]


def header(record):
    lines=['// Generated from unchanged original0x583f0; presentation boundaries recorded.',
           '#pragma once','#include <array>','#include <cstdint>',
           'namespace g1_support_fixture {',
           'struct Row { std::uint8_t count,index; std::int32_t elapsed,delta; std::uint8_t next_index; std::int32_t next_elapsed; std::uint8_t action; };',
           'inline constexpr std::array<Row,%d> rows{{'%len(record['updates'])]
    for row in record['updates']:
        args=row['input'];out=row['output']
        lines.append('    {%d,%d,%d,%d,%d,%d,%d},'%tuple(args+out))
    lines.extend(['}};','} // namespace g1_support_fixture',''])
    return '\n'.join(lines)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('binary','libgcc','unicorn-dir','base-oracle','output','fixture-header'):
        parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args();sys.path.insert(0,str(args.unicorn_dir))
    import unicorn
    spec=importlib.util.spec_from_file_location('support_combat_base',args.base_oracle)
    base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)
    image,gcc=args.binary.read_bytes(),args.libgcc.read_bytes()
    if sha(image)!=base.IMAGE_SHA or sha(gcc)!=base.LIBGCC_SHA:raise ValueError('original input hash mismatch')

    class Recorder(base.OriginalCombat):
        def __init__(self):
            self.events=[];self.strings={};self.ready_calls=0
            super().__init__(image,gcc)
            self.renderer=self.alloc(0x100);self.w32(self.map+0x298,self.renderer)
            self.helpers=[self.alloc(0x80) for _ in range(6)]
            self.effects=[self.alloc(0x80) for _ in range(23)]
            self.w32(self.renderer+0x28,self.array(self.effects))
            self.w32(self.map+0x3c,self.array([0]*6));self.w32(self.map+0x40,6)
            self.vector(self.map+0x12c,[100+i for i in range(23)],'h')
            self.vector(self.map+0x3d8,[200,130],'B')
            for i,pointer in enumerate(self.helpers):
                self.w8(pointer+9,i+2);self.w8(pointer+0x3b,i+3);self.w8(pointer+0x3c,i+4)
                self.w32(pointer+0x40,10+i);self.w32(pointer+0x44,-20-i)
        def hook(self,instance,address,size,user):
            ar=self.ar
            r0,r1,r2,r3=[instance.reg_read(getattr(ar,'UC_ARM_REG_R'+str(i))) for i in range(4)]
            handled=True
            if address==0x5d176:
                self.ready_calls+=1;handled=False # Execute its actual unconditional true body.
            elif address==0x5e288:
                self.events.append(['effect_animation',self.effects.index(r0),r1,r2,0])
            elif address==0x5cb5a:
                self.events.append(['focus',self.r8(r1+9),self.r8(r1+0x3b),self.r8(r1+0x3c),
                                    signed(self.r32(r1+0x40)),signed(self.r32(r1+0x44)),r2&255])
                handled=False # Keep its actual selected-unit store; stop at camera endpoint.
            elif address==0x5cb26:pass
            elif address==0x557c4:
                self.events.append(['cursor',r1&255,r2&255]);handled=False
            elif address==0x54f94:pass
            elif address==0x301aa:self.strings[r0]=[signed(r1)]
            elif address==0x60688:
                self.strings[r0]=list(self.strings[r1]);self.events.append(['text_assign',*self.strings[r1]])
            elif address==0x60838:
                if bytes(instance.mem_read(r1,2))!=b' \0':raise RuntimeError('unexpected support separator')
                self.strings[r0].append(' ');self.events.append(['text_space'])
            elif address==0x608b2:
                self.strings[r0].extend(self.strings[r1]);self.events.append(['text_append',*self.strings[r1]])
            elif address==0x604b4:pass
            elif address==0x51e46:self.events.append(['selection_cleanup',self.r8(r1+9)])
            elif address==0x51faa:self.events.append(['select_attacker',self.r8(r1+9)])
            elif address==0x5537a:self.events.append(['reset_interaction'])
            elif address==0x5c9a8:
                self.events.append(['clear_focus']);handled=False
            elif address==0x652a4:self.events.append(['cursor_animation',r0])
            elif address==0x58172:self.events.append(['main_battle'])
            elif address==0xba210:pass
            else:handled=False
            if handled:
                instance.reg_write(ar.UC_ARM_REG_PC,instance.reg_read(ar.UC_ARM_REG_LR));return
            super().hook(instance,address,size,user)
        def configure_support(self,count,index,elapsed):
            for i,pointer in enumerate(self.helpers):
                self.w32(self.r32(self.map+0x3c)+4*i,pointer if i<count else 0)
            self.w8(self.map+0x48,int(count!=0));self.w32(self.map+0x44,count)
            self.w32(self.map+0x4c,index);self.w32(self.map+0x50,elapsed)
            self.events=[];self.ready_calls=0
        def update(self,count,index,elapsed,delta):
            self.configure_support(count,index,elapsed)
            self.call(0x583f0,[self.map,delta]);self.check_text()
            if self.ready_calls!=1:raise RuntimeError('original readiness call count changed')
            action=2 if ['main_battle'] in self.events else 1 if self.events else 0
            return {'input':[count,index,elapsed,delta],
                    'output':[self.r32(self.map+0x4c),signed(self.r32(self.map+0x50)),action],
                    'events':list(self.events)}
        def begin(self,count):
            self.configure_support(count,3,999)
            # Audited caller locals at the entry to the support-only branch.
            # Stop before the separate first-use tutorial check / function epilogue.
            sp=0x21ff000;self.w32(sp+0x14,self.map);self.w32(sp+0x10,self.helpers[0])
            self.call(0x586aa,[],stop=0x58796 if count else 0x5885c);self.check_text()
            return {'count':count,'index':self.r32(self.map+0x4c),
                    'elapsed':signed(self.r32(self.map+0x50)),'events':list(self.events)}

    original=Recorder();begins=[original.begin(count) for count in range(7)]
    updates=[]
    for count in range(1,7):
        for index in range(min(count,5)):
            for elapsed in (-2147483648,-1,0,1,1969,1999,2000,2147483647):
                for delta in (-1,0,1,30,1999,2000,100000,2147483647):
                    updates.append(original.update(count,index,elapsed,delta))
    report={'schema':1,'kind':'original tactical support presentation contract',
            'original_image_sha256':base.IMAGE_SHA,'original_libgcc_sha256':base.LIBGCC_SHA,
            'oracle_sha256':sha(Path(__file__).read_bytes()),'base_oracle_sha256':sha(args.base_oracle.read_bytes()),
            'unicorn_version':unicorn.__version__,'instruction_patches':0,'original_code_readonly_unchanged':True,
            'entries':['0x586aa..0x58796 support entry branch','0x586aa..0x5885c no-support handoff branch',
                       '0x583f0 complete update function','0x5d176 actual always-true helper'],
            'capacity_evidence':'0x4bc22 allocates24 bytes;0x4bc30 stores6;0x58466 ends at index5',
            'boundaries':['2D effect animation start','camera endpoint','text construction/copy/append/destruction',
                          'selection UI','interaction reset UI','cursor appearance','main combat handoff','unwind bookkeeping'],
            'begins':begins,'updates':updates,'gpu_execution':False,'combat_or_rng_execution':False,
            'tutorial_popup_scope':'0x58796..0x5884e separately instruction-reviewed; not executed here'}
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(report,indent=2)+'\n')
    args.fixture_header.parent.mkdir(parents=True,exist_ok=True);args.fixture_header.write_text(header(report))
    print(json.dumps({'begins':len(begins),'updates':len(updates),'state_scalars':len(updates)*3,'passed':True}))


if __name__=='__main__':main()
