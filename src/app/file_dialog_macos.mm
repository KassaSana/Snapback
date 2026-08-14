#if defined(__APPLE__)

#include "app/file_dialog.hpp"

#import <AppKit/AppKit.h>

namespace snapback {

bool file_dialog_supported() {
    return true;
}

FileDialogResult pick_open_file(const FileDialogOptions& options) {
    return detail::pick_open_file_native(options);
}

FileDialogResult pick_save_file(const FileDialogOptions& options) {
    return detail::pick_save_file_native(options);
}

namespace detail {

FileDialogResult pick_open_file_native(const FileDialogOptions& options) {
    @autoreleasepool {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        [panel setCanChooseFiles:YES];
        [panel setCanChooseDirectories:NO];
        [panel setAllowsMultipleSelection:NO];

        if (!options.title.empty()) {
            [panel setTitle:[NSString stringWithUTF8String:options.title.c_str()]];
        }
        if (!options.default_path.empty()) {
            NSString* dirStr = [NSString stringWithUTF8String:options.default_path.c_str()];
            if (dirStr != nil) {
                [panel setDirectoryURL:[NSURL fileURLWithPath:dirStr isDirectory:YES]];
            }
        }
        if (!options.default_name.empty()) {
            [panel setNameFieldStringValue:[NSString stringWithUTF8String:options.default_name.c_str()]];
        }

        NSInteger res = [panel runModal];
        if (res == NSModalResponseOK) {
            NSURL* url = [[panel URLs] firstObject];
            if (url != nil && [url path] != nil) {
                return FileDialogResult{true, false, std::string([[url path] UTF8String]), ""};
            }
        }
        return FileDialogResult{false, true, "", "Cancelled by user"};
    }
}

FileDialogResult pick_save_file_native(const FileDialogOptions& options) {
    @autoreleasepool {
        NSSavePanel* panel = [NSSavePanel savePanel];

        if (!options.title.empty()) {
            [panel setTitle:[NSString stringWithUTF8String:options.title.c_str()]];
        }
        if (!options.default_path.empty()) {
            NSString* dirStr = [NSString stringWithUTF8String:options.default_path.c_str()];
            if (dirStr != nil) {
                [panel setDirectoryURL:[NSURL fileURLWithPath:dirStr isDirectory:YES]];
            }
        }
        if (!options.default_name.empty()) {
            [panel setNameFieldStringValue:[NSString stringWithUTF8String:options.default_name.c_str()]];
        }

        NSInteger res = [panel runModal];
        if (res == NSModalResponseOK) {
            NSURL* url = [panel URL];
            if (url != nil && [url path] != nil) {
                return FileDialogResult{true, false, std::string([[url path] UTF8String]), ""};
            }
        }
        return FileDialogResult{false, true, "", "Cancelled by user"};
    }
}

}  // namespace detail
}  // namespace snapback

#endif  // __APPLE__
