"""Give identical firmware sources stable names in GCC debug information."""
from pathlib import Path

Import("env")

project = Path(env.subst("$PROJECT_DIR")).resolve()
repo = project.parents[1]
framework = Path(env.PioPlatform().get_package_dir("framework-arduinoespressif32")).resolve()
env.AppendUnique(CCFLAGS=[
    f"-fdebug-prefix-map={repo.as_posix()}=/enevina-lite",
    f"-fdebug-prefix-map={framework.as_posix()}=/enevina-sdk/arduino-esp32",
])
