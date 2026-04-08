#include "AppRouter.h"

namespace {
constexpr uint8_t kNotificationsHidden = 0;
constexpr uint8_t kNotificationsStrip = 1;
constexpr uint8_t kNotificationsExpanded = 2;
}  // namespace

bool appRouterRenderActiveApp(
    SystemState &state,
    bool oledOnly,
    Stream &out,
    AppRouterRenderContext &context) {
    if (state.launcher.activeAppId.equalsIgnoreCase("desktop")) {
        return context.renderDesktopScreen(state, oledOnly, out);
    }

    if (state.launcher.activeAppId.equalsIgnoreCase("notifications")) {
        return context.renderNotificationsScreen(state, oledOnly, out);
    }

    if (state.launcher.activeAppId.equalsIgnoreCase("launcher")) {
        return context.renderLauncherScreen(state, oledOnly, out);
    }

    if (state.launcher.activeAppId.equalsIgnoreCase("settings")) {
        return context.renderSettingsScreen(state, oledOnly, out);
    }

    if (state.launcher.activeAppId.equalsIgnoreCase("file-manager")) {
        return context.renderFileManagerScreen(state, oledOnly, out);
    }

    if (state.launcher.activeAppId.equalsIgnoreCase("web-upload")) {
        return context.renderWebUploadScreen(state, oledOnly, out);
    }

    return context.renderPlaceholderApp(state, out);
}

bool appRouterHandleBackInput(
    SystemState &state,
    uint8_t rawCode,
    uint8_t settingsViewHome,
    uint8_t settingsViewWifiList,
    uint8_t settingsViewWifiPassword,
    uint8_t settingsViewWifiSelectList,
    uint8_t settingsViewBluetoothList,
    uint8_t settingsViewBluetoothSelectList,
    AppRouterInputContext &context,
    Stream &out) {
    if (!context.isLeftArrowCode(rawCode)) {
        return false;
    }

    if (state.launcher.activeAppId.equalsIgnoreCase("notifications")) {
        state.launcher.activeAppId = "desktop";
        state.notifications.viewMode = kNotificationsStrip;
        state.settings.lastMessage = "upozorneni sbaleno";
        context.renderDesktopScreen(state, false, out);
        return true;
    }

    if (state.launcher.activeAppId.equalsIgnoreCase("desktop") && state.notifications.viewMode == kNotificationsStrip) {
        state.notifications.viewMode = kNotificationsHidden;
        state.settings.lastMessage = "lista zavrena";
        context.renderDesktopScreen(state, false, out);
        return true;
    }

    if (state.launcher.activeAppId.equalsIgnoreCase("settings")) {
        if (state.settings.viewMode == settingsViewWifiPassword) {
            state.settings.viewMode = settingsViewWifiSelectList;
            state.settings.wifiPassword = "";
            state.settings.lastMessage = "password canceled";
            context.renderSettingsScreen(state, false, out);
            return true;
        }

        if (state.settings.viewMode == settingsViewWifiSelectList) {
            state.settings.viewMode = settingsViewWifiList;
            state.settings.lastMessage = "wifi list closed";
            context.renderSettingsScreen(state, false, out);
            return true;
        }

        if (state.settings.viewMode == settingsViewBluetoothSelectList) {
            state.settings.viewMode = settingsViewBluetoothList;
            state.settings.lastMessage = "device list closed";
            context.renderSettingsScreen(state, false, out);
            return true;
        }

        if (state.settings.viewMode == settingsViewBluetoothList || state.settings.viewMode == settingsViewWifiList) {
            state.settings.viewMode = settingsViewHome;
            state.settings.lastMessage = "back";
            context.renderSettingsScreen(state, false, out);
            return true;
        }

        state.launcher.activeAppId = "launcher";
        state.settings.lastMessage = "back to launcher";
        context.renderLauncherScreen(state, false, out);
        return true;
    }

    if (state.launcher.activeAppId.equalsIgnoreCase("web-upload")) {
        context.stopWebUploadServer(out);
        state.launcher.activeAppId = "launcher";
        state.settings.lastMessage = "back to launcher";
        context.renderLauncherScreen(state, false, out);
        return true;
    }

    if (!state.launcher.activeAppId.equalsIgnoreCase("launcher")) {
        state.launcher.activeAppId = "launcher";
        context.renderLauncherScreen(state, false, out);
        return true;
    }

    return false;
}

bool appRouterHandleDesktopDirectionalInput(
    SystemState &state,
    uint8_t rawCode,
    AppRouterInputContext &context,
    Stream &out) {
    if (!state.launcher.activeAppId.equalsIgnoreCase("desktop")) {
        return false;
    }

    if (context.isDownArrowCode(rawCode)) {
        state.notifications.viewMode = kNotificationsHidden;
        state.launcher.activeAppId = "launcher";
        state.settings.lastMessage = "launcher";
        context.renderLauncherScreen(state, false, out);
        return true;
    }

    if (context.isRightArrowCode(rawCode)) {
        if (state.notifications.viewMode == kNotificationsHidden) {
            state.notifications.viewMode = kNotificationsStrip;
            state.settings.lastMessage = "lista upozorneni";
            context.renderDesktopScreen(state, false, out);
            return true;
        }

        state.notifications.viewMode = kNotificationsExpanded;
        state.launcher.activeAppId = "notifications";
        state.settings.lastMessage = "upozorneni full";
        context.renderNotificationsScreen(state, false, out);
        return true;
    }

    return false;
}
