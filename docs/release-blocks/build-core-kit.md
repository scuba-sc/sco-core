<!-- Template for the release-block named build-core-kit; edit here, then run tools/release_docs.py --write. -->
```sh
SDK_DIR={{archive_core}}
SDK_DIR=${SDK_DIR%.zip}   # the folder the archive extracted
cmake -S "$SDK_DIR" -B sdk-build -DCMAKE_BUILD_TYPE=Release
cmake --build sdk-build --config Release
cmake --install sdk-build --config Release --prefix sdk-out
python3 - <<'PY'
from pathlib import Path
import shutil
shutil.copytree(Path('{{archive_core}}').stem + '/template', 'first_plugin')
for name in ('plugin.c', 'plugin.ini', 'CMakeLists.txt'):
    path = Path('first_plugin') / name
    path.write_text(path.read_text().replace('my_plugin', 'first_plugin'))
PY
cmake -S first_plugin -B first-plugin-build -DSCO_SDK="$PWD/$SDK_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build first-plugin-build --config Release
cmake --install first-plugin-build --config Release --prefix first-plugin-out
```
