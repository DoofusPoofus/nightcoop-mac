#include "plugin/TestKeys.hpp"

#include <objc/message.h>
#include <objc/runtime.h>

#include <initializer_list>

namespace nightcoop_mac
{
void PressKey(unsigned short keyCode, const char* characters)
{
    PostKey(keyCode, characters, true);
    PostKey(keyCode, characters, false);
}

void PostKey(unsigned short keyCode, const char* characters, bool down)
{
    using Send = id (*)(id, SEL);
    using KeyEvent = id (*)(id, SEL, unsigned long, double, double, unsigned long, double, long, id, id, id, bool, unsigned short);
    using Post = void (*)(id, SEL, id, bool);
    using MakeString = id (*)(id, SEL, const char*);
    id application = reinterpret_cast<Send>(objc_msgSend)(reinterpret_cast<id>(objc_getClass("NSApplication")), sel_registerName("sharedApplication"));
    id window = application ? reinterpret_cast<Send>(objc_msgSend)(application, sel_registerName("keyWindow")) : nullptr;
    if (!window && application)
    {
        window = reinterpret_cast<Send>(objc_msgSend)(application, sel_registerName("mainWindow"));
    }
    const long windowNumber = window ? reinterpret_cast<long (*)(id, SEL)>(objc_msgSend)(window, sel_registerName("windowNumber")) : 0;
    id space = reinterpret_cast<MakeString>(objc_msgSend)(reinterpret_cast<id>(objc_getClass("NSString")), sel_registerName("stringWithUTF8String:"), characters);
    for (const unsigned long type : {down ? 10ul : 11ul})
    {
        id event = reinterpret_cast<KeyEvent>(objc_msgSend)(reinterpret_cast<id>(objc_getClass("NSEvent")),
                                                            sel_registerName("keyEventWithType:location:modifierFlags:timestamp:windowNumber:context:characters:"
                                                                             "charactersIgnoringModifiers:isARepeat:keyCode:"),
                                                            type, 0.0, 0.0, 0ul, 0.0, windowNumber, nullptr, space, space, false, keyCode);
        if (application && event)
        {
            reinterpret_cast<Post>(objc_msgSend)(application, sel_registerName("postEvent:atStart:"), event, false);
        }
    }
}

void PressSpaceKey()
{
    PressKey(49, " ");
}

void PressCrouchKey()
{
    PressKey(8, "c");
}
}
