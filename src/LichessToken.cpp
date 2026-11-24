#include "LichessClient.h"

/* Native credential storage; no original entry point. The personal access token lives
 * in the macOS login Keychain as a generic password; it is never written to save
 * files, preferences or logs. Other platforms keep it for the session only (or read
 * BC_LICHESS_TOKEN at startup). */
#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>

namespace
{
    constexpr const char* kService = "Battle Chess Lichess token";
    constexpr const char* kAccount = "lichess";

    struct Cf
    {
        CFTypeRef ref = nullptr;

        ~Cf()
        {
            if (ref)
                CFRelease(ref);
        }
    };

    CFMutableDictionaryRef query()
    {
        CFMutableDictionaryRef dictionary = CFDictionaryCreateMutable(
            nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        CFStringRef service = CFStringCreateWithCString(nullptr, kService, kCFStringEncodingUTF8);
        CFStringRef account = CFStringCreateWithCString(nullptr, kAccount, kCFStringEncodingUTF8);
        CFDictionarySetValue(dictionary, kSecClass, kSecClassGenericPassword);
        CFDictionarySetValue(dictionary, kSecAttrService, service);
        CFDictionarySetValue(dictionary, kSecAttrAccount, account);
        CFRelease(service);
        CFRelease(account);
        return dictionary;
    }
}

std::string loadLichessToken()
{
    Cf search{query()};
    auto dictionary = (CFMutableDictionaryRef)search.ref;
    CFDictionarySetValue(dictionary, kSecReturnData, kCFBooleanTrue);
    CFDictionarySetValue(dictionary, kSecMatchLimit, kSecMatchLimitOne);
    Cf data;
    if (SecItemCopyMatching(dictionary, &data.ref) != errSecSuccess || !data.ref)
        return "";
    auto bytes = (CFDataRef)data.ref;
    return std::string(reinterpret_cast<const char*>(CFDataGetBytePtr(bytes)),
                       size_t(CFDataGetLength(bytes)));
}

bool saveLichessToken(const std::string& token)
{
    forgetLichessToken();
    Cf add{query()};
    auto dictionary = (CFMutableDictionaryRef)add.ref;
    Cf data{
        CFDataCreate(nullptr, reinterpret_cast<const UInt8*>(token.data()),
                     CFIndex(token.size()))
    };
    CFDictionarySetValue(dictionary, kSecValueData, data.ref);
    CFDictionarySetValue(dictionary, kSecAttrAccessible, kSecAttrAccessibleWhenUnlocked);
    return SecItemAdd(dictionary, nullptr) == errSecSuccess;
}

void forgetLichessToken()
{
    Cf remove{query()};
    SecItemDelete((CFDictionaryRef)remove.ref);
}

bool lichessTokenStorageAvailable()
{
    return true;
}
#else
std::string loadLichessToken()
{
    return "";
}

bool saveLichessToken(const std::string&)
{
    return false;
}

void forgetLichessToken()
{
}

bool lichessTokenStorageAvailable()
{
    return false;
}
#endif
