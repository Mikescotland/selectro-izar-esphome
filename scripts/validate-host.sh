#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
make test
node --check tests/portal_syntax.js
g++ -std=c++20 -Wall -Wextra -Itests/stubs -fsyntax-only components/selectro_izar/selectro_izar.cpp components/selectro_izar/izar_decoder.cpp
python3 - <<'PYCODE'
import yaml
for name in ('examples/selectro_izar_gateway.yaml','examples/selectro_izar_gateway_all_alarms.yaml'):
    config=yaml.safe_load(open(name).read().replace('!secret ',''))
    assert len(config['text_sensor'])==30
    assert len(config['sensor'])==60
    print('YAML parsed',name)
PYCODE
