#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
make test
node --check tests/portal_syntax.js
node tests/test_portal_selection.js
node tests/test_maintenance.js
g++ -std=c++20 -Wall -Wextra -DUSE_ESP32 -DUSE_WEBSERVER_AUTH -Itests/stubs -fsyntax-only components/selectro_izar/selectro_izar.cpp components/selectro_izar/izar_decoder.cpp
python3 - <<'PYCODE'
import yaml
for name in ('examples/selectro_izar_gateway.yaml','examples/selectro_izar_gateway_all_alarms.yaml'):
    config=yaml.safe_load(open(name).read().replace('!secret ',''))
    assert len(config['text_sensor'])==40
    assert len(config['sensor'])==70
    assert len([s for s in config['binary_sensor'] if s.get('name','').endswith('Stale')])==10
    assert config['selectro_izar']['admin_username'] == 'web_username'
    print('YAML parsed',name)
PYCODE
