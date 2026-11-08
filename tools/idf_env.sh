# Usage: source tools/idf_env.sh
export IDF_PATH="${IDF_PATH:-/Users/npisarev/Projects/ESP32/ESP-IDF/v5.5/esp-idf}"
# Homebrew python moved to 3.15; reuse the existing ESP-IDF venv built for 3.13.
export IDF_PYTHON_ENV_PATH="${IDF_PYTHON_ENV_PATH:-$HOME/.espressif/python_env/idf5.5_py3.13_env}"
. "$IDF_PATH/export.sh" > /dev/null
