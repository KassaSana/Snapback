// AppKit backend for open_url.hpp. Roadmap 8.14.
#if defined(__APPLE__)

#include "app/open_url.hpp"

#import <AppKit/AppKit.h>

namespace snapback {
namespace detail {

bool open_external_url_impl(const std::string& url) {
    if (![NSThread isMainThread]) return false;

    @autoreleasepool {
        NSString* value = [NSString stringWithUTF8String:url.c_str()];
        if (value == nil) return false;

        NSURL* ns_url = [NSURL URLWithString:value];
        if (ns_url == nil) return false;

        return [[NSWorkspace sharedWorkspace] openURL:ns_url] == YES;
    }
}

}  // namespace detail
}  // namespace snapback

#endif  // __APPLE__
