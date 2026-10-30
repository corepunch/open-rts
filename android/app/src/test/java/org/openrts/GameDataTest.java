package org.openrts;

import static org.junit.Assert.*;
import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

public class GameDataTest {
    @Rule public TemporaryFolder temporary = new TemporaryFolder();

    private ByteArrayInputStream zip(String... names) throws IOException {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        try (ZipOutputStream zip = new ZipOutputStream(bytes)) {
            for (String name : names) {
                zip.putNextEntry(new ZipEntry(name));
                zip.write(new byte[] {1, 2, 3});
                zip.closeEntry();
            }
        }
        return new ByteArrayInputStream(bytes.toByteArray());
    }

    @Test public void installsOnlyTheSelectedGameAndPreservesCase() throws IOException {
        File staging = new File(temporary.getRoot(), "import");
        File root = new File(temporary.getRoot(), "data/DCOLONY");
        GameData.install(zip("data/DCOLONY/SCENARIO/HUMAN/HUMAN01.MAP", "data/WAR2/ALAMO.PUD"),
            "data/DCOLONY/", staging, root);
        assertArrayEquals(new byte[] {1, 2, 3},
            Files.readAllBytes(new File(root, "SCENARIO/HUMAN/HUMAN01.MAP").toPath()));
        assertFalse(new File(root, "ALAMO.PUD").exists());
        assertFalse(staging.exists());
    }

    @Test public void rejectsTraversalAndDiscardsPartialImport() throws IOException {
        File staging = new File(temporary.getRoot(), "import");
        File root = new File(temporary.getRoot(), "data/DCOLONY");
        assertThrows(IOException.class, () -> GameData.install(
            zip("data/DCOLONY/valid", "data/DCOLONY/../escaped"), "data/DCOLONY/", staging, root));
        assertFalse(root.exists());
        assertFalse(staging.exists());
        assertFalse(new File(temporary.getRoot(), "escaped").exists());
    }

    @Test public void requiresTheExpectedDataTree() throws IOException {
        File staging = new File(temporary.getRoot(), "import");
        File root = new File(temporary.getRoot(), "data/DCOLONY");
        assertThrows(IOException.class, () -> GameData.install(zip("wrong/file"),
            "data/DCOLONY/", staging, root));
        assertFalse(root.exists());
        assertFalse(staging.exists());
    }

    @Test public void preservesInstalledData() throws IOException {
        File root = temporary.newFolder("installed");
        File original = new File(root, "original");
        Files.write(original.toPath(), new byte[] {9});
        assertThrows(IOException.class, () -> GameData.install(zip("data/DCOLONY/new"),
            "data/DCOLONY/", new File(temporary.getRoot(), "import"), root));
        assertArrayEquals(new byte[] {9}, Files.readAllBytes(original.toPath()));
        assertFalse(new File(root, "new").exists());
    }

    @Test public void supportsNestedDarkReignRoot() throws IOException {
        File root = new File(temporary.getRoot(), "data/REIGN/dark");
        GameData.install(zip("data/REIGN/dark/SHELL.RLD"), "data/REIGN/dark/",
            new File(temporary.getRoot(), "import"), root);
        assertTrue(new File(root, "SHELL.RLD").isFile());
    }
}
