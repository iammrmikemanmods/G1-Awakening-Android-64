package com.iammrmikeman.g1awakening;

import android.content.res.AssetManager;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.AtomicMoveNotSupportedException;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.HashSet;
import java.util.Locale;
import java.util.Set;

/** Imports the original packaged bytes; never writes into the save directory. */
final class AssetInstaller {
    interface Progress { void update(int completed, int total); }
    private static final int BUFFER_SIZE = 64 * 1024;
    private static final String MANIFEST = "game/asset-manifest.json";

    private AssetInstaller() {}

    static File install(AssetManager assets, File filesDir, Progress progress)
            throws IOException, JSONException {
        byte[] manifestBytes;
        try (InputStream input = assets.open(MANIFEST, AssetManager.ACCESS_STREAMING);
             ByteArrayOutputStream output = new ByteArrayOutputStream()) {
            byte[] buffer = new byte[8192];
            int count;
            while ((count = input.read(buffer)) != -1) {
                if (output.size() + count > 8 * 1024 * 1024) {
                    throw new IOException("The packaged asset manifest is too large.");
                }
                output.write(buffer, 0, count);
            }
            manifestBytes = output.toByteArray();
        }
        JSONObject manifest = new JSONObject(new String(manifestBytes, StandardCharsets.UTF_8));
        if (manifest.getInt("schema") != 1) {
            throw new IOException("Unsupported packaged asset manifest schema.");
        }
        JSONArray entries = manifest.getJSONArray("files");
        if (entries.length() == 0 || entries.length() > 100000) {
            throw new IOException("Invalid packaged asset count: " + entries.length());
        }
        File root = new File(filesDir, "game");
        ensureDirectory(root);
        String prefix = root.getCanonicalPath() + File.separator;
        Set<String> paths = new HashSet<>();
        for (int index = 0; index < entries.length(); index++) {
            checkInterrupted();
            JSONObject entry = entries.getJSONObject(index);
            String path = entry.getString("path");
            validateRelativePath(path);
            if (!paths.add(path) || path.equals("asset-manifest.json")) {
                throw new IOException("Duplicate or reserved asset path: " + path);
            }
            long expectedSize = entry.getLong("size");
            String expectedHash = entry.getString("sha256").toLowerCase(Locale.ROOT);
            if (expectedSize < 0 || !expectedHash.matches("[0-9a-f]{64}")) {
                throw new IOException("Invalid integrity metadata for " + path);
            }
            File destination = new File(root, path);
            if (!destination.getCanonicalPath().startsWith(prefix)) {
                throw new IOException("Asset path escapes game directory: " + path);
            }
            if (!matches(destination, expectedSize, expectedHash)) {
                ensureDirectory(destination.getParentFile());
                File temporary = File.createTempFile("g1-import-", ".part", destination.getParentFile());
                try {
                    MessageDigest digest = sha256();
                    long written = 0;
                    try (InputStream input = assets.open("game/" + path, AssetManager.ACCESS_STREAMING);
                         FileOutputStream output = new FileOutputStream(temporary)) {
                        byte[] buffer = new byte[BUFFER_SIZE];
                        int count;
                        while ((count = input.read(buffer)) != -1) {
                            checkInterrupted();
                            written += count;
                            if (written > expectedSize) {
                                throw new IOException("Packaged asset exceeds its declared size: " + path);
                            }
                            digest.update(buffer, 0, count);
                            output.write(buffer, 0, count);
                        }
                        output.getFD().sync();
                    }
                    if (written != expectedSize || !hex(digest.digest()).equals(expectedHash)) {
                        throw new IOException("Packaged asset integrity failed: " + path);
                    }
                    replace(temporary, destination);
                } finally {
                    Files.deleteIfExists(temporary.toPath());
                }
            }
            progress.update(index + 1, entries.length());
        }
        for (String required : new String[] {
                "GameData/iphone.big", "GameData/terrain-tables.bin", "G1OpeningXR.mp4"}) {
            if (!paths.contains(required)) {
                throw new IOException("Required original asset is missing from the package: " + required);
            }
        }
        File marker = File.createTempFile("g1-manifest-", ".part", root);
        try {
            try (FileOutputStream output = new FileOutputStream(marker)) {
                output.write(manifestBytes);
                output.getFD().sync();
            }
            replace(marker, new File(root, "asset-manifest.json"));
        } finally {
            Files.deleteIfExists(marker.toPath());
        }
        return root;
    }

    private static void validateRelativePath(String path) throws IOException {
        if (path.isEmpty() || path.startsWith("/") || path.contains("\\") || path.indexOf('\0') >= 0) {
            throw new IOException("Invalid asset path: " + path);
        }
        for (String part : path.split("/", -1)) {
            if (part.isEmpty() || part.equals(".") || part.equals("..")) {
                throw new IOException("Invalid asset path component: " + path);
            }
        }
    }

    private static boolean matches(File file, long expectedSize, String expectedHash) throws IOException {
        if (!file.isFile() || file.length() != expectedSize) return false;
        MessageDigest digest = sha256();
        try (InputStream input = new FileInputStream(file)) {
            byte[] buffer = new byte[BUFFER_SIZE];
            int count;
            while ((count = input.read(buffer)) != -1) {
                checkInterrupted();
                digest.update(buffer, 0, count);
            }
        }
        return hex(digest.digest()).equals(expectedHash);
    }

    private static MessageDigest sha256() {
        try {
            return MessageDigest.getInstance("SHA-256");
        } catch (NoSuchAlgorithmException impossible) {
            throw new AssertionError("Android must provide SHA-256", impossible);
        }
    }

    private static String hex(byte[] bytes) {
        StringBuilder value = new StringBuilder(bytes.length * 2);
        for (byte item : bytes) value.append(String.format(Locale.ROOT, "%02x", item & 0xff));
        return value.toString();
    }

    private static void ensureDirectory(File directory) throws IOException {
        if (!directory.isDirectory() && !directory.mkdirs()) {
            throw new IOException("Cannot create app directory: " + directory.getName());
        }
    }

    private static void replace(File source, File destination) throws IOException {
        try {
            Files.move(source.toPath(), destination.toPath(),
                    StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING);
        } catch (AtomicMoveNotSupportedException ignored) {
            Files.move(source.toPath(), destination.toPath(), StandardCopyOption.REPLACE_EXISTING);
        }
    }

    private static void checkInterrupted() throws IOException {
        if (Thread.currentThread().isInterrupted()) throw new IOException("Asset import cancelled.");
    }
}
