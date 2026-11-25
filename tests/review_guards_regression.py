"""Static guards for fixes that depend on engine or network state."""

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]

CHECKS = {
    "manifest cdn url validation": (
        "src/utils/HttpClient.cpp",
        ["if (!isUrlSafe(entryOut.cdnUrl)) return false;", "if (!isUrlSafe(cdnUrl))"],
    ),
    "url safety helper": (
        "src/utils/HttpUrl.hpp",
        ["inline bool isPublicHttpUrl", "only global unicast", "single-integer IPv4"],
    ),
    "overlay host validation": (
        "src/features/twitch-requests/services/StreamOverlayServer.cpp",
        ["Invalid Host", "421", "localhost:21680"],
    ),
    "gradient type check": (
        "src/hooks/LevelCell.cpp",
        ["psg = typeinfo_cast<PaimonShaderGradient*>(target);", "m_thumbInvalidated"],
    ),
    "mod previews lifetime": (
        "src/features/mod-previews/hooks/ModPreviewsListener.cpp",
        ["WeakRef<PreviewRibbon>", "slot->cancelLoad();", "popup->isRunning()"],
    ),
    "factory reset backup": (
        "src/core/FactoryResetActions.cpp",
        ["backupUserContent(saveDir)", "factory-reset-backup-"],
    ),
    "icon import bounds": (
        "src/features/icon-maker/services/IconShare.cpp",
        ["kMaxImportBytes", "kMaxEntries", "unzip.extractTo"],
    ),
    "global icon atomic cache": (
        "src/features/global-icon/services/GlobalIconStorage.cpp",
        ["writeAtomically", "validCachedFile"],
    ),
    "progress font applied once": (
        "src/features/progressbar/services/ProgressBarManager.cpp",
        ["lb->getFntFile()"],
    ),
    "song cover failure callbacks": (
        "src/features/menu-music/services/SongCoverCache.cpp",
        ["callback({}, false)", "dispatchCallbacks(batch, {}, false)"],
    ),
    "layout atomic save": (
        "src/features/main-menu-layout/services/MainMenuLayoutManager.cpp",
        ["writeAtomically"],
    ),
    "search history cap": (
        "src/features/search-history/SearchHistory.cpp",
        ["kMaxHistory"],
    ),
    "ban cache clock skew": (
        "src/core/BanGate.cpp",
        ["now < record.checkedAt"],
    ),
    "versus session heap lifetime": (
        "src/features/versus/services/VersusSession.cpp",
        ["new VersusSession()"],
    ),
    "collab host-only settings": (
        "src/features/collab-editor/CollabManager.cpp",
        ["host->second.isHost"],
    ),
    "profile upload owns account": (
        "src/features/profiles/services/ProfileImageService.cpp",
        ["accountID != accountManager->m_accountID"],
    ),
    "clean tasks resets coalescing": (
        "src/utils/HttpClient.cpp",
        ["m_manifestFetchInFlight = false;", "m_inflightDownloads.clear();"],
    ),
    "ban identity consistency": (
        "src/core/BanGate.cpp",
        ["paimon::ban::sameAccountName(accountName, playerName)", "game->m_playerUserID"],
    ),
    "ban cache mirror repair": (
        "src/core/BanGate.cpp",
        ["needsRepair", "persistRecord(*cache);", "sameRecord(fileRead.record, *cache)"],
    ),
    "ban response binding": (
        "src/utils/BanVerdict.hpp",
        ["expectedAccountID", "expectedCheck", "sanitizeDisplayText"],
    ),
    "ban viewer token": (
        "src/utils/HttpClient.cpp",
        ['req.header("X-Viewer-Token", m_viewerToken);',
         'parseBanVerdict(res.string().unwrapOr(""), reqAccountID, nonce)'],
    ),
    "ban moderation authorization": (
        "src/utils/HttpClient.cpp",
        ["if (!paimon::modauth::isVerified())",
         "paimon::modauth::verifiedUsername()",
         'postWithAuth("/api/admin/ban"'],
    ),
    "banned popup persistence": (
        "src/features/moderation/ui/BannedPopup.cpp",
        ["g_activeBannedPopup", "isBannedPopupActive", "PaimonPopup::onExit();"],
    ),
    "banned keybind block": (
        "src/utils/ExtendedKeybind.cpp",
        ["if (paimon::ban::isBanned()) return;"],
    ),
    "ban list hardening": (
        "src/features/moderation/ui/BanListPopup.cpp",
        ["kMaxBanListEntries", "ban.list.error", "m_unbanPending"],
    ),
    "ban popup hardening": (
        "src/features/moderation/ui/BanUserPopup.cpp",
        ["m_pending", "ban.verify_required", "isSafeAccountName(m_username)"],
    ),
    "ambient button id exclusion guard": (
        "src/features/dynamic-ambient/services/AmbientDecorator.cpp",
        ['if (id == "tab-background") return false;', 'bool const isButtonNode = typeinfo_cast<ButtonSprite*>(node) != nullptr'],
    ),
}


def check_beat_shader_layers(failures):
    path = ROOT / "src/features/beat-shaders/services/BeatShaderManager.cpp"
    if not path.is_file():
        failures.append(f"beat shader layers: missing {path.relative_to(ROOT)}")
        return
    text = path.read_text(encoding="utf-8-sig")
    available_match = re.search(r"availableLayers\(\) const \{(.*?)\n\}", text, re.S)
    lookup_match = re.search(r"CCLayer\* findLayerByKey\(.*?\n\}", text, re.S)
    if not available_match or not lookup_match:
        failures.append("beat shader layers: could not parse availableLayers/findLayerByKey")
        return
    available = set(re.findall(r'\{"([\w-]+)",\s*"', available_match.group(1)))
    resolved = set(re.findall(r'key == "([\w-]+)"', lookup_match.group(0)))
    if available != resolved:
        failures.append(
            "beat shader layers: availableLayers != findLayerByKey "
            f"(missing={sorted(available - resolved)}, extra={sorted(resolved - available)})"
        )


def main():
    failures = []
    for name, (relative, snippets) in CHECKS.items():
        path = ROOT / relative
        if not path.is_file():
            failures.append(f"{name}: missing {relative}")
            continue
        text = path.read_text(encoding="utf-8-sig")
        missing = [snippet for snippet in snippets if snippet not in text]
        if missing:
            failures.append(f"{name}: {relative} lacks {missing}")

    check_beat_shader_layers(failures)

    if failures:
        for failure in failures:
            print("FAIL:", failure)
        return 1
    print(f"PASS: {len(CHECKS) + 1} review guards")
    return 0


if __name__ == "__main__":
    sys.exit(main())
