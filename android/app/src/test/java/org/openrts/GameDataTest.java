package org.openrts;

import static org.junit.Assert.*;
import java.io.*;
import java.nio.file.Files;
import java.util.*;
import java.util.zip.*;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

public class GameDataTest {
    @Rule public TemporaryFolder temporary = new TemporaryFolder();
    private static final String DC = "data/DCOLONY/";
    private File staging() { return new File(temporary.getRoot(), "import"); }
    private File root() { return new File(temporary.getRoot(), "installed"); }

    private byte[] zip(String... names) throws IOException {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        try (ZipOutputStream zip = new ZipOutputStream(bytes)) {
            for (String name : names) {
                // STORED makes payload corruption deterministic and tests our own CRC check.
                byte[] payload = {1, 2, 3};
                CRC32 crc = new CRC32(); crc.update(payload);
                ZipEntry entry = new ZipEntry(name);
                entry.setMethod(ZipEntry.STORED); entry.setSize(payload.length); entry.setCrc(crc.getValue());
                zip.putNextEntry(entry); zip.write(payload); zip.closeEntry();
            }
        }
        return bytes.toByteArray();
    }
    private String[] names(String prefix, String... extra) throws IOException {
        List<String> result = new ArrayList<>();
        for (String name : GameData.required(prefix)) result.add(prefix + name.split("\\|")[0]);
        Collections.addAll(result, extra);
        return result.toArray(new String[0]);
    }
    private void install(byte[] bytes) throws IOException {
        GameData.install(new ByteArrayInputStream(bytes), DC, staging(), root());
    }
    private void rejected(byte[] bytes) {
        assertThrows(IOException.class, () -> install(bytes));
        assertFalse(root().exists()); assertFalse(staging().exists());
    }

    @Test public void installsOnlySelectedGameAndPreservesCase() throws IOException {
        install(zip(names(DC, "data/WAR2/ALAMO.PUD")));
        assertTrue(GameData.isReady(root(), DC));
        assertArrayEquals(new byte[] {1,2,3}, Files.readAllBytes(new File(root(), "SPRITES/TROOPER1.SPR").toPath()));
        assertFalse(new File(root(), "ALAMO.PUD").exists()); assertFalse(staging().exists());
    }
    @Test public void supportsAllSevenGameRoots() throws IOException {
        for (String prefix : new String[] {DC, "data/REIGN/dark/", "data/7LEGION/", "data/KKND/", "data/WAR2/", "data/STARCRAFT/", "data/Z/"}) {
            GameData.install(new ByteArrayInputStream(zip(names(prefix))), prefix, staging(), root());
            assertTrue(GameData.isReady(root(), prefix));
            for (File file : root().listFiles()) remove(file);
            assertTrue(root().delete());
        }
    }
    private void remove(File file) {
        File[] children = file.listFiles(); if (children != null) for (File child : children) remove(child);
        assertTrue(file.delete());
    }
    @Test public void rejectsEachMissingPrerequisite() throws IOException {
        String[] all = names(DC);
        for (int missing = 0; missing < all.length; ++missing) {
            List<String> subset = new ArrayList<>(Arrays.asList(all)); subset.remove(missing);
            rejected(zip(subset.toArray(new String[0])));
        }
    }
    @Test public void rejectsWrongRoot() throws IOException { rejected(zip("wrong/file")); }
    @Test public void rejectsEmptyZip() throws IOException { rejected(zip()); }
    @Test public void rejectsNonZip() { rejected(new byte[] {1,2,3}); }
    @Test public void rejectsTraversal() throws IOException { rejected(zip(names(DC, DC + "../escaped"))); }
    @Test public void rejectsNestedTraversal() throws IOException { rejected(zip(names(DC, DC + "folder/../../escaped"))); }
    @Test public void rejectsAbsolutePath() throws IOException { rejected(zip(names(DC, DC + "/absolute"))); }
    @Test public void rejectsBackslash() throws IOException { rejected(zip(names(DC, DC + "folder\\file"))); }
    @Test public void rejectsDrivePath() throws IOException { rejected(zip(names(DC, DC + "C:/file"))); }
    @Test public void rejectsReceiptSpoofing() throws IOException { rejected(zip(names(DC, DC + ".open-rts-ready"))); }
    @Test public void rejectsExcessiveNesting() throws IOException { rejected(zip(names(DC, DC + String.join("/", Collections.nCopies(33, "deep"))))); }
    @Test public void rejectsTruncatedCentralDirectory() throws IOException {
        byte[] bytes = zip(names(DC)); rejected(Arrays.copyOf(bytes, bytes.length - 30));
    }
    @Test public void rejectsDamagedEntry() throws IOException {
        byte[] bytes = zip(names(DC));
        int offset = 30 + (bytes[26] & 255) + ((bytes[27] & 255) << 8);
        bytes[offset] ^= 0x40; rejected(bytes);
    }
    @Test public void rejectsDuplicatePaths() throws IOException {
        byte[] bytes = zip(names(DC, DC + "a", DC + "b"));
        byte[] name = (DC + "b").getBytes(java.nio.charset.StandardCharsets.UTF_8);
        for (int i = 0; i <= bytes.length - name.length; ++i) {
            boolean equal = true;
            for (int j = 0; j < name.length; ++j) equal &= bytes[i+j] == name[j];
            if (equal) bytes[i + name.length - 1] = 'a';
        }
        rejected(bytes);
    }
    @Test public void rejectsFileDirectoryCollision() throws IOException {
        rejected(zip(names(DC, DC + "collision", DC + "collision/child")));
    }
    @Test public void boundsCompressedExpandedFileAndEntrySizes() throws IOException {
        byte[] bytes = zip(names(DC));
        long[][] limits = {{bytes.length-1, 100, 100, 100}, {10000, 8, 100, 100},
            {10000, 100, 2, 100}, {10000, 100, 100, 2}};
        for (long[] limit : limits) {
            assertThrows(IOException.class, () -> GameData.install(new ByteArrayInputStream(bytes), DC,
                staging(), root(), limit[0], limit[1], limit[2], (int)limit[3]));
            assertFalse(root().exists()); assertFalse(staging().exists());
        }
    }
    @Test public void preservesInstalledDataOnFailureAndAllowsReplacement() throws IOException {
        install(zip(names(DC, DC + "original")));
        assertThrows(IOException.class, () -> install(zip("wrong/file")));
        assertTrue(GameData.isReady(root(), DC)); assertTrue(new File(root(), "original").exists());
        install(zip(names(DC, DC + "replacement")));
        assertFalse(new File(root(), "original").exists()); assertTrue(new File(root(), "replacement").exists());
    }
    @Test public void repairsLegacyPartialInstallation() throws IOException {
        assertTrue(root().mkdir()); Files.write(new File(root(), "partial").toPath(), new byte[]{9});
        assertFalse(GameData.isReady(root(), DC)); install(zip(names(DC)));
        assertTrue(GameData.isReady(root(), DC)); assertFalse(new File(root(), "partial").exists());
    }
    @Test public void recoversInterruptedCommit() throws IOException {
        install(zip(names(DC)));
        assertTrue(root().renameTo(new File(root().getPath() + ".previous")));
        assertTrue(GameData.isReady(root(), DC));
    }
    @Test public void rechecksRequiredFilesBeforePlay() throws IOException {
        install(zip(names(DC)));
        assertTrue(new File(root(), "SPRITES/TROOPER1.SPR").delete());
        assertFalse(GameData.isReady(root(), DC));
    }
    @Test public void rejectsZeroLengthRequiredFile() throws IOException {
        install(zip(names(DC)));
        try (FileOutputStream file = new FileOutputStream(new File(root(), "SPRITES/TROOPER1.SPR"))) {}
        assertFalse(GameData.isReady(root(), DC));
    }
    @Test public void cleansUpAfterProviderReadFailure() {
        InputStream broken = new InputStream() { @Override public int read() throws IOException { throw new IOException("provider disconnected"); } };
        assertThrows(IOException.class, () -> GameData.install(broken, DC, staging(), root()));
        assertFalse(root().exists()); assertFalse(staging().exists());
    }
}
