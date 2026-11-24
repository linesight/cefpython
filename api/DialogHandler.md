[API categories](API-categories.md) | [API index](API-index.md)


# DialogHandler (interface)

Implement this interface to handle events related to file dialogs. The methods of this class will be called on the UI thread.


Table of contents:
* [FileDialogCallback](#filedialogcallback)
  * [Continue](#continue)
  * [Cancel](#cancel)
* [Callbacks](#callbacks)
  * [OnFileDialog](#onfiledialog)


## FileDialogCallback

Callback interface used for asynchronous continuation of file dialog
requests.


### Continue

| Parameter | Type |
| --- | --- |
| file_paths | list |
| __Return__ | void |

Continue the file selection. |file_paths| is a list or tuple of
absolute paths, each a str or an os.PathLike object such as
pathlib.Path: one path, or one or more with FILE_DIALOG_OPEN_MULTIPLE.
A bytes path raises TypeError. More than one path in another mode raises
ValueError. An empty list is the same as calling Cancel().
A relative path raises ValueError, because Chromium does not resolve it:
it terminates the renderer process for a file input and saves a download
relative to the working directory.

The paths are not checked further. A path to a file that does not exist
is accepted, and the overwrite prompt of FILE_DIALOG_SAVE is part of the
default dialog only.

This method can be called on any thread, also after OnFileDialog has
returned.


### Cancel

| | |
| --- | --- |
| __Return__ | void |

Cancel the file selection.


## Callbacks


### OnFileDialog

| Parameter | Type |
| --- | --- |
| browser | [Browser](Browser.md) |
| mode | int |
| title | str |
| default_file_path | str |
| accept_filters | list[str] |
| accept_extensions | list[str] |
| accept_descriptions | list[str] |
| callback | FileDialogCallback |
| __Return__ | bool |

Called to run a file chooser dialog. |mode| represents the type of dialog
to display. |title| is the title to be used for the dialog and may be
empty to show the default title ("Open" or "Save" depending on the mode).
|default_file_path| is the path with optional directory and/or file name
component that should be initially selected in the dialog.
|accept_filters| are used to restrict the selectable file types and may be
any combination of valid lower-cased MIME types (e.g. "text/*" or
"image/*") and individual file extensions (e.g. ".txt" or ".png").
|accept_extensions| provides the semicolon-delimited expansion of MIME
types to file extensions (if known, or empty string otherwise).
|accept_descriptions| provides the descriptions for MIME types (if known,
or empty string otherwise). For example, the "image/*" mime type might
have extensions ".png;.jpg;.bmp;..." and description "Image Files".
|accept_filters|, |accept_extensions| and |accept_descriptions| will all
be the same size. To display a custom dialog return True and execute
|callback| either inline or at a later time. To display the default dialog
return False. If this method returns False it may be called an additional
time for the same dialog (both before and after MIME type expansion).
Before the expansion |accept_extensions| holds the |accept_filters|
values unchanged, with an empty string for MIME types ending in "/*".

If you return True and never call the callback, the dialog is canceled
when the callback object is released. An exception raised in
OnFileDialog is passed to sys.excepthook and counts as returning False.

Known limitations in CEF 147:
* On Linux, `showDirectoryPicker()` arrives with mode FILE_DIALOG_OPEN
  and an empty title and default path. Not checked on other platforms.
* On Linux with off-screen rendering, selecting a folder for
  `<input type="file" webkitdirectory>` crashes the application, with the
  default dialog too. Call Cancel() for FILE_DIALOG_OPEN_FOLDER in an
  off-screen application on Linux. On Windows this works, with the
  default dialog and with Continue(): after a folder is selected,
  Chromium asks "Upload N files to this site?" in a separate window.
  Not checked on macOS.

The `mode` constants available in the cefpython module:
* FILE_DIALOG_OPEN: Requires that the file exists before allowing the user
  to pick it.
* FILE_DIALOG_OPEN_MULTIPLE: Like Open, but allows picking multiple files
  to open.
* FILE_DIALOG_OPEN_FOLDER: Like Open, but selects a folder to open.
* FILE_DIALOG_SAVE: Allows picking a nonexistent file, and prompts to
  overwrite if the file already exists.

Example of a handler that selects a file without showing a dialog:

```python
import os

class DialogHandler(object):
    def OnFileDialog(self, browser, mode, callback, **_):
        callback.Continue([os.path.join(os.path.expanduser("~"), "file.txt")])
        return True

browser.SetClientHandler(DialogHandler())
```

The default dialog is provided by CEF, cefpython does not implement one.
