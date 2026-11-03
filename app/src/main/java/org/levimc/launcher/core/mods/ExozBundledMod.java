package org.levimc.launcher.core.mods;

import android.content.Context;
import android.content.pm.PackageInfo;
import android.os.Build;
import android.util.Log;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

/**
 * Installs the Exoz core mod (libexozmod.so, built into this APK) into the current game version's mods
 * folder, so it shows up in the in-game Mod Menu like any other native mod. Re-copies the library whenever
 * the app is updated. A user who disables the mod keeps it disabled (ModManager remembers by mod id).
 */
public final class ExozBundledMod {
    private static final String TAG = "ExozBundledMod";
    public static final String FOLDER = "exoz_client_core";
    private static final String LIB = "libexozmod.so";

    private static final String MANIFEST =
            "{\n"
            + "  \"type\": \"preload-native\",\n"
            + "  \"name\": \"Exoz Client Core\",\n"
            + "  \"author\": \"Exoz\",\n"
            + "  \"version\": \"1.0.0\",\n"
            + "  \"description\": \"Exoz HUD (FPS, frame time, graph) and frame-rate hint.\",\n"
            + "  \"entry\": \"" + LIB + "\",\n"
            + "  \"minecraft_versions\": []\n"
            + "}\n";

    private ExozBundledMod() {}

    /** @return true if files were (re)written, so the caller should rescan mods. */
    public static boolean ensureInstalled(Context context, File modsDir) {
        if (context == null || modsDir == null) return false;
        try {
            File dir = new File(modsDir, FOLDER);
            File lib = new File(dir, LIB);
            File stampFile = new File(dir, ".exoz_stamp");
            String stamp = currentStamp(context);
            if (lib.isFile() && stamp.equals(readText(stampFile))) return false;

            if (!dir.isDirectory() && !dir.mkdirs()) return false;
            if (!copyLibrary(context, lib)) {
                Log.w(TAG, "libexozmod.so not found in the APK; the Exoz core mod is unavailable");
                return false;
            }
            writeText(new File(dir, "manifest.json"), MANIFEST);
            writeText(stampFile, stamp);
            Log.i(TAG, "installed into " + dir.getAbsolutePath());
            return true;
        } catch (Exception e) {
            Log.w(TAG, "install failed", e);
            return false;
        }
    }

    private static String currentStamp(Context context) throws Exception {
        PackageInfo pi = context.getPackageManager().getPackageInfo(context.getPackageName(), 0);
        return pi.versionName + ":" + pi.lastUpdateTime;
    }

    private static boolean copyLibrary(Context context, File target) throws IOException {
        File tmp = new File(target.getPath() + ".tmp");
        boolean ok = copyFromApk(context.getApplicationInfo().sourceDir, tmp);
        String[] splits = context.getApplicationInfo().splitSourceDirs;
        if (!ok && splits != null) {
            for (String split : splits) {
                if (copyFromApk(split, tmp)) { ok = true; break; }
            }
        }
        if (!ok) {   // libraries extracted to disk (older installs / legacy packaging)
            File extracted = new File(context.getApplicationInfo().nativeLibraryDir, LIB);
            if (extracted.isFile()) {
                copyStream(new FileInputStream(extracted), tmp);
                ok = true;
            }
        }
        if (!ok) return false;
        if (target.exists() && !target.delete()) return false;
        return tmp.renameTo(target);
    }

    private static boolean copyFromApk(String apkPath, File tmp) throws IOException {
        if (apkPath == null) return false;
        File apk = new File(apkPath);
        if (!apk.isFile()) return false;
        ZipFile zip = new ZipFile(apk);
        try {
            for (String abi : Build.SUPPORTED_ABIS) {
                ZipEntry entry = zip.getEntry("lib/" + abi + "/" + LIB);
                if (entry != null) {
                    copyStream(zip.getInputStream(entry), tmp);
                    return true;
                }
            }
            return false;
        } finally {
            zip.close();
        }
    }

    private static void copyStream(InputStream in, File out) throws IOException {
        OutputStream os = new FileOutputStream(out);
        try {
            byte[] buf = new byte[64 * 1024];
            int n;
            while ((n = in.read(buf)) > 0) os.write(buf, 0, n);
        } finally {
            os.close();
            in.close();
        }
    }

    private static String readText(File f) {
        if (!f.isFile()) return "";
        try {
            InputStream in = new FileInputStream(f);
            try {
                byte[] data = new byte[(int) Math.min(f.length(), 4096)];
                int n = in.read(data);
                return n > 0 ? new String(data, 0, n, StandardCharsets.UTF_8).trim() : "";
            } finally {
                in.close();
            }
        } catch (IOException e) {
            return "";
        }
    }

    private static void writeText(File f, String text) throws IOException {
        OutputStream os = new FileOutputStream(f);
        try {
            os.write(text.getBytes(StandardCharsets.UTF_8));
        } finally {
            os.close();
        }
    }
}
