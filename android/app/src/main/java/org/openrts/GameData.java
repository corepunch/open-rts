package org.openrts;

import java.io.*;
import java.util.*;
import java.util.zip.*;

final class GameData {
    // Bound compressed input, expanded output, metadata and individual allocations.
    static final long ZIP_LIMIT = 8L << 30, DATA_LIMIT = 16L << 30, FILE_LIMIT = 2L << 30;
    static final int ENTRY_LIMIT = 200000;
    private static final String RECEIPT = ".open-rts-ready";

    // These are startup prerequisites from each C loader, not a retail-file checksum list.
    static String[] required(String prefix) throws IOException {
        switch (prefix) {
        case "data/REIGN/dark/": return new String[] {"shell/SHELL.RLI", "shell/SHELL.RLD", "shell/SHELLCFG.H", "local/MLSTRING.CFG", "scenario/FIXED/M01F/M01F.SCN", "graphics/SPRITES.FTG"};
        case "data/DCOLONY/": return new String[] {"INTRFACE/INTROE", "SCENARIO/HUMAN/HUMAN01.MAP", "SPRITES/TROOPER1.SPR"};
        case "data/7LEGION/": return new String[] {"DATA/MAPT.000", "GFX/LTROOP.BIM", "GFX/TILES.BIM", "GFX/PAL1.COL"};
        case "data/KKND/": return new String[] {"LEVELS/640/SURV_01.LVL", "LEVELS/640/SPRITES.LVL"};
        case "data/WAR2/": return new String[] {"DATA/MAINDAT.WAR", "DATA/REZDAT.WAR"};
        case "data/STARCRAFT/": return new String[] {"install/campaign/terran/terran01/staredit/scenario.chk", "native/arr/units.dat|install/arr/units.dat", "native/arr/images.dat|install/arr/images.dat", "native/arr/images.tbl|install/arr/images.tbl", "native/scripts/iscript.bin|install/scripts/iscript.bin", "native/rez/glumain.bin|install/rez/glumain.bin"};
        case "data/Z/": return new String[] {"LEVEL01/LEVEL01.MAP", "LEVELS.DAT", "SPRITES.RSC", "SHEADFX.PAC", "WARDATA.PAC"};
        default: throw new IOException("Unknown game data directory.");
        }
    }

    private static void validate(File root, String prefix) throws IOException {
        for (String choices : required(prefix)) {
            boolean found = false;
            for (String name : choices.split("\\|")) {
                File file = new File(root, name);
                found |= file.isFile() && file.length() > 0;
            }
            if (!found) throw new IOException("Missing or empty required file: " + choices);
        }
    }

    private static File previous(File root) { return new File(root.getPath() + ".previous"); }

    private static void recover(File root) throws IOException {
        File backup = previous(root);
        if (!root.exists() && backup.exists() && !backup.renameTo(root))
            throw new IOException("Cannot recover previous game data.");
    }

    static synchronized boolean isReady(File root, String prefix) {
        try {
            recover(root);
            if (!new File(root, RECEIPT).isFile()) return false;
            validate(root, prefix);
            return true;
        } catch (IOException error) { return false; }
    }

    static synchronized void install(InputStream input, String prefix, File staging, File root) throws IOException {
        install(input, prefix, staging, root, ZIP_LIMIT, DATA_LIMIT, FILE_LIMIT, ENTRY_LIMIT);
    }

    // Explicit limits let regression tests exercise the same boundary checks with small archives.
    static synchronized void install(InputStream input, String prefix, File staging, File root,
                                    long zipLimit, long dataLimit, long fileLimit, int entryLimit) throws IOException {
        required(prefix);
        recover(root);
        deleteTree(staging);
        if (!staging.mkdirs()) throw new IOException("Cannot create import folder.");
        File archive = new File(staging, "archive.zip"), tree = new File(staging, "tree");
        try {
            if (!tree.mkdir()) throw new IOException("Cannot create extraction folder.");
            byte[] buffer = new byte[65536];
            // ZipFile requires a complete central directory: truncated streams cannot be committed.
            try (FileOutputStream output = new FileOutputStream(archive)) {
                copy(input, output, buffer, zipLimit, staging, null);
            }
            try (ZipFile zip = new ZipFile(archive)) {
                if (zip.size() > entryLimit) throw new IOException("ZIP contains too many entries.");
                Set<String> names = new HashSet<>();
                long total = 0;
                Enumeration<? extends ZipEntry> entries = zip.entries();
                while (entries.hasMoreElements()) {
                    ZipEntry entry = entries.nextElement();
                    String name = entry.getName();
                    if (name.length() > 1024) throw new IOException("ZIP path is too long.");
                    if (!name.startsWith(prefix)) continue;
                    String relative = name.substring(prefix.length());
                    if (relative.isEmpty() && entry.isDirectory()) continue;
                    String[] parts = relative.split("/", -1);
                    if (parts.length > 32 || relative.startsWith("/") || relative.indexOf('\\') >= 0 ||
                        relative.indexOf(':') >= 0 || relative.indexOf('\0') >= 0 || relative.equals(RECEIPT))
                        throw new IOException("Invalid ZIP path: " + name);
                    for (int i = 0; i < parts.length; ++i)
                        if (parts[i].equals("..") || parts[i].equals(".") ||
                            (parts[i].isEmpty() && !(entry.isDirectory() && i == parts.length - 1)))
                            throw new IOException("Invalid ZIP path: " + name);
                    File file = new File(tree, relative);
                    if (!file.getCanonicalPath().startsWith(tree.getCanonicalPath() + File.separator))
                        throw new IOException("ZIP contains a path outside the game folder.");
                    if (!names.add(file.getCanonicalPath())) throw new IOException("Duplicate ZIP path: " + name);
                    if (entry.isDirectory()) continue;
                    if (entry.getSize() < 0 || entry.getSize() > fileLimit || entry.getSize() > dataLimit - total)
                        throw new IOException("ZIP exceeds game data limits.");
                    File parent = file.getParentFile();
                    if (!parent.isDirectory() && !parent.mkdirs()) throw new IOException("Cannot create game folder.");
                    CRC32 crc = new CRC32();
                    long size;
                    try (InputStream source = zip.getInputStream(entry); FileOutputStream output = new FileOutputStream(file)) {
                        size = copy(source, output, buffer, Math.min(fileLimit, dataLimit - total), staging, crc);
                    }
                    if (size != entry.getSize() || crc.getValue() != entry.getCrc())
                        throw new IOException("Damaged ZIP entry: " + name);
                    total += size;
                }
            }
            validate(tree, prefix);
            if (!new File(tree, RECEIPT).createNewFile()) throw new IOException("Cannot mark import complete.");
            File parent = root.getParentFile(), backup = previous(root);
            if (!parent.isDirectory() && !parent.mkdirs()) throw new IOException("Cannot create data folder.");
            deleteTree(backup);
            if (root.exists() && !root.renameTo(backup)) throw new IOException("Cannot preserve previous data.");
            if (!tree.renameTo(root)) {
                recover(root);
                throw new IOException("Cannot install game data.");
            }
            deleteTree(backup);
        } finally { deleteTree(staging); }
    }

    private static long copy(InputStream source, OutputStream output, byte[] buffer, long limit,
                             File storage, CRC32 crc) throws IOException {
        long total = 0;
        int count;
        while ((count = source.read(buffer)) != -1) {
            if (Thread.currentThread().isInterrupted()) throw new IOException("Import cancelled.");
            if (count > limit - total) throw new IOException("ZIP exceeds game data limits.");
            long available = storage.getUsableSpace();
            if (available < count + (16L << 20)) throw new IOException("Not enough free space for import.");
            output.write(buffer, 0, count);
            if (crc != null) crc.update(buffer, 0, count);
            total += count;
        }
        return total;
    }

    private static void deleteTree(File file) throws IOException {
        // Import paths have bounded depth and never contain filesystem links.
        File[] children = file.listFiles();
        if (children != null) for (File child : children) deleteTree(child);
        if (file.exists() && !file.delete()) throw new IOException("Cannot remove incomplete import.");
    }
}
