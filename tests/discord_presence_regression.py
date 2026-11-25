"""Discord integration contracts; does not compile or contact Discord."""
from pathlib import Path
import ast
import re

ROOT = Path(__file__).resolve().parents[1]
FEATURE = ROOT / 'src/features/discord-presence'


def contracts():
    manager = '\n'.join((FEATURE / 'services' / name).read_text(encoding='utf-8') for name in (
        'DiscordPresenceManager.cpp', 'DiscordSceneContext.cpp', 'DiscordPresenceComposer.cpp'))
    popup = (FEATURE / 'ui/DiscordConfigPopup.cpp').read_text(encoding='utf-8')
    client = (FEATURE / 'services/DiscordIpcClient.cpp').read_text(encoding='utf-8')
    assert 'getCurrentPercent()' in manager, 'Publish current attempt progress, not only best.'
    assert 'previewActivity()' in manager and 'previewActivity()' in popup
    assert 'Stereo Madness (Best 34%)' not in popup and '00:42 elapsed' not in popup
    assert 'ipc.poll()' in manager, 'Poll unchanged sessions to detect asynchronous replies/restarts.'
    assert 'buildPage(' in popup and 'showPage(' in popup
    assert 'weakTry' in popup, 'Upload retry callbacks must not retain their own function.'
    assert 'm_uploadGeneration' in popup and 'WeakRef<DiscordConfigPopup>' in popup
    assert '!parsed.unwrap().isObject()' in popup, 'Reject non-object profile roots before field access.'
    assert 'activity:null' not in client  # JSON must be quoted.
    assert r'\"activity\":null' in client
    model = (FEATURE / 'model/PresenceCustomization.hpp').read_text(encoding='utf-8')
    assert 'expandTemplate' in model and 'validLink' in model and 'kBooleanOptions' in model
    assert 'configuration.flag("discord-rpc-private-mode")' in manager
    assert 'configuration.flag("discord-rpc-share-private-levels")' in manager
    assert 'configuration.flag("discord-rpc-show-attempts")' in manager
    assert 'm_lastActivity' in manager and 'activity == m_lastActivity' in manager
    assert 'nextSend' in manager and 'nextRefresh' in manager
    assert 'mailbox->takeClearRequest()' in manager
    assert 'm_pInScene' in manager
    assert 'm_practicePercent.value()' not in manager
    assert '.activitySnapshot(' in manager and 'PaiDrawManager::get().snapshot()' not in manager
    assert 'm_connected{false}' in (FEATURE / 'services/DiscordIpcClient.hpp').read_text(encoding='utf-8')
    assert 'operation->buffer.data()' in client and 'cancelledOperations().push_back(std::move(operation))' in client
    booleans = re.findall(r'BooleanOption\{"([^"]+)"', model)
    texts = re.findall(r'TextOption\{"([^"]+)"', model)
    assert len(booleans + texts) == len(set(booleans + texts)), 'Setting keys must be unique.'
    keys = set(booleans + texts)
    assert 'discord-rpc-enabled' not in keys, 'Import/reset must not enable broadcasting.'
    used = set(re.findall(r'(?:flag|text|toggle|input|cycle)\("(discord-rpc-[^"]+)"', manager + popup))
    assert used <= keys, used - keys
    settings = (FEATURE / 'services/DiscordPresenceSettings.hpp').read_text(encoding='utf-8')
    assert 'kBooleanOptions' in settings and 'validChoice(' in settings
    assert 'm_receiveBuffer' in client and 'kOpPing' in client
    ast.parse(Path(__file__).read_text(encoding='utf-8'))
    print('PASS: Discord presence integration contracts (no compilation/network).')


if __name__ == '__main__':
    contracts()
