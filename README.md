# FO4-Wrench

FO4-Wrench is a configurable mod for Fallout 4, containing fixes and behavior
modifications.

* Adds options for modifying scaling behavior of each individual UI clip.
  This fixes scaling for most of the default UI elements in non-16:9 aspect
  ratios, and allows UI modders to create a single version of their mods that
  will work with all aspect ratios.
* Fixes the placement and scaling of UI backdrops in non-standard aspect ratios,
  so they no longer appear misplaced.

It is implemented as a wrapper around XInput since the game voluntarily loads
the XInput dll, and the XInput API is tiny enough that wrapping it is just about
effortless. Simply drop the dll in the game folder to get started. See the
[configuration section](#configuration) for fine-tuning.

[Screenshots](#screenshots)

## Building

MSVC is the only supported compiler, and Visual Studio 2015 *may* be required as
I've made no effort to support older versions. Compiling should just be a matter
of opening the solution and building. All the dependencies are embedded in the
project.

## Configuration

FO4-Wrench is configured through a [TOML](/toml-lang/toml) file named
**Wrench.toml** inside your **Documents\My Games\Fallout4** configuration
folder. If that folder does not exist, either create it or simply launch the
game as that will create it for you. All configuration options are optional.

* **XInput.Path**
  > The path to the *real* XInput1_3.dll.

* **Features.`feature`**
  > Boolean enabling or disabling individual mod features.

    * `BackdropFix`: Enables or disables the backdrop aspect ratio fix.
    * `UiScale`: Enables or disables UI clip scaling.

* **UiScale.`filename`**:
  > The scale mode to use for the UI clip with the given `filename`. The path
    should use forward slashes, not backslashes. Requires UI clip scaling to
    be enabled. Possible options are:

    * `"NoScale"`: No scaling, the UI clip's native size will be used.
    * `"ShowAll"`: Resize the UI clip to fill the viewport, while maintaining
      aspect ratio. No clipping is applied, so UI clips may use the full
      viewport size.
    * `"ExactFit"`: Resize the UI clip, disregarding the aspect ratio. If the
      clip's aspect ratio is not the same as that of the viewport, it will be
      stretched to cover the viewport.
    * `"NoBorder"`: Resize the UI clip to fill the viewport, while
      maintaining aspect ratio. Contents outside the normal aspect ratio may be
      clipped.

### Default configuration

Note that the below values are used by default even when not specified. You only
need to specify them to override them. Also note that UI scale modes are
particularly lacking in this list. They all use the game's default unless
overridden.

```toml
[XInput]
Path = '%WINDIR%\system32\XInput1_3.dll'

[Features]
BackdropFix = true
UiScale = true
```

### Recommended configuration

Recommendation for 21:9 resolutions. Some UI mods may require alternative
settings.

```toml
[UiScale]
"Interface/ButtonBarMenu.swf" = "ShowAll"
"Interface/ExamineMenu.swf" = "ShowAll"
"Interface/FaderMenu.swf" = "ExactFit"
"Interface/HUDMenu.swf" = "ShowAll"
"Interface/PromptMenu.swf" = "ShowAll"
"Interface/Workshop.swf" = "ShowAll"
"Interface/Workshop_CaravanMenu.swf" = "ShowAll"
```

## Libraries

* [cpptoml](https://github.com/skystrife/cpptoml)
* [udis86](https://github.com/vmt/udis86)

## Screenshots

![](doc/mainmenu-default.jpg)
Default main menu in 21:9.

![](doc/mainmenu-modded.jpg)
Default main menu in 21:9, with this mod in the recommended configuration.

![](doc/hud-default.jpg)
Default HUD in 21:9.

![](doc/hud-modded.jpg)
Default HUD in 21:9, with this mod in the recommended configuration.

![](doc/container-default.jpg)
Default container UI in 21:9.

![](doc/container-modded.jpg)
Default container UI in 21:9, with this mod in the recommended configuration.
