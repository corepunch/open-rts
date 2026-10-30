package org.openrts;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

final class GameData {
    static void install(InputStream input, String prefix, File staging, File root) throws IOException {
        if (root.exists()) throw new IOException("Data is already installed. Clear app storage to replace it.");
        deleteTree(staging);
        if (!staging.mkdirs()) throw new IOException("Cannot create import folder.");
        String destination = staging.getCanonicalPath() + File.separator;
        int files = 0;
        try (ZipInputStream zip = new ZipInputStream(input)) {
            ZipEntry entry;
            byte[] buffer = new byte[65536];
            while ((entry = zip.getNextEntry()) != null) {
                if (!entry.getName().startsWith(prefix) || entry.isDirectory()) continue;
                File file = new File(staging, entry.getName().substring(prefix.length()));
                if (!file.getCanonicalPath().startsWith(destination))
                    throw new IOException("ZIP contains a path outside the game folder.");
                File parent = file.getParentFile();
                if (!parent.isDirectory() && !parent.mkdirs())
                    throw new IOException("Cannot create game folder.");
                try (FileOutputStream output = new FileOutputStream(file)) {
                    int count;
                    while ((count = zip.read(buffer)) != -1) output.write(buffer, 0, count);
                }
                ++files;
            }
            if (files == 0) throw new IOException("ZIP must contain " + prefix);
            File parent = root.getParentFile();
            if (!parent.isDirectory() && !parent.mkdirs())
                throw new IOException("Cannot create data folder.");
            if (!staging.renameTo(root)) throw new IOException("Cannot install game data.");
        } finally {
            deleteTree(staging);
        }
    }

    private static void deleteTree(File file) {
        File[] children = file.listFiles();
        if (children != null) for (File child : children) deleteTree(child);
        file.delete();
    }
}
