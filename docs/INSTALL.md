# Installing Blendity

Download the file for your computer from the [README](../README.md#download) or the
[Releases](https://github.com/EmilianoManaloIV/blendity/releases) page, then follow the steps for your system.
You don't need Blender or anything else: the installer brings everything Blendity uses.

Not sure which Mac you have? Apple menu > **About This Mac**: "Chip: Apple M..." means an Apple chip. Macs with an
Intel processor aren't supported yet. (Every version, with what changed in it, is on the
[Releases](https://github.com/EmilianoManaloIV/blendity/releases) page; the "Source code" files there are for
developers.)

If a link says "Not Found", open the [Releases](https://github.com/EmilianoManaloIV/blendity/releases) page, scroll
to **Assets** under the newest release and click the file for your computer: the one ending in **`.exe`** for
Windows, **`.dmg`** for a Mac, **`.AppImage`** for Linux.

## Install it

<details open>
<summary><b>Windows</b></summary>

1. Open your **Downloads** folder and double-click **`Blendity-Setup-Windows.exe`**.
2. If a blue box says **"Windows protected your PC"**, click **More info**, then **Run anyway**. (Windows shows this
   for programs that aren't signed with a paid certificate; Blendity isn't yet.)
3. If Windows asks whether to install for you only or for everyone, pick **for me only** (no administrator needed).
4. Accept the licence and click **Next** through the steps. Keep **"Blendity with Blender's libraries
   (recommended)"**. Tick **Create a desktop shortcut** if you want one.
5. Click **Install**, then **Finish**. Blendity opens.

Next time, open it from the **Start** menu (type "Blendity"). **To uninstall:** Settings > Apps > Installed apps >
Blendity > **Uninstall**. Your projects are kept.
</details>

<details>
<summary><b>Mac</b></summary>

1. Double-click **`Blendity-macos.dmg`** in your Downloads.
2. In the window that opens, drag the **Blendity** icon onto the **Applications** folder, then close the window.
3. Open **Applications** and double-click **Blendity**.
4. The first time, macOS says it can't check that the app is free of malware (Blendity isn't signed with a paid
   Apple certificate yet):
   - **macOS 15 (Sequoia) or newer:** click **Done**. Open **System Settings > Privacy & Security**, scroll down to
     "Blendity was blocked..." and click **Open Anyway**, then **Open Anyway** again and enter your Mac's password.
   - **macOS 14 or older:** right-click (or Control-click) Blendity in Applications, choose **Open**, then **Open**.

   You only do this once. **If macOS says Blendity "is damaged":** open the **Terminal** app, paste this line and
   press Return, then open Blendity again:

   ```bash
   xattr -dr com.apple.quarantine /Applications/Blendity.app
   ```

**To uninstall:** drag Blendity from Applications to the Bin.
</details>

<details>
<summary><b>Linux</b></summary>

1. In your **Downloads** folder, right-click **`Blendity-linux-x86_64.AppImage`** and choose **Properties**.
2. Under **Permissions**, turn on **Allow executing file as program** (or "Executable as Program").
3. Close the window and double-click the file. Blendity opens. (You can move the file anywhere first, for example to
   a folder called Applications in your home folder.)

If double-clicking does nothing, download
[the plain folder](https://github.com/EmilianoManaloIV/blendity/releases/download/libraries/blendity-linux-x86_64.tar.gz)
instead, right-click it > **Extract Here**, open the new folder and double-click **Blendity**. **To uninstall:** delete the file (or the
folder).
</details>
## First steps

- Your work is saved in a project folder, **BlendityProject** in your home folder, unless you open another.
- Press **F1** for the **Learn** tab: short interactive lessons for each part of Blendity.
- **Updates:** Blendity checks once a day and asks before installing anything (Help > Check for Updates). Help >
  Version History goes back to an older version if a new one gives you trouble.
- Blendity runs with or without Blender's libraries (faster rendering, denoising, video export...): your choice in
  Preferences > Libraries. Without them, or offline, it uses its own code.
## From the command line

```bash
Blendity --layout Learning      # start with the Learn tab beside the Scene view
Blendity --scene Assets/Scenes/SampleScene.scene
Blendity --scale 1.5            # force the UI scale (default follows the OS DPI)
Blendity --headless-screenshot out.png --cmd "select Cube" --cmd "shading rendered" --frames 30   # no window
```
