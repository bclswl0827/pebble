import struct
import subprocess
import sys
from pathlib import Path

from SCons.Script import AlwaysBuild, Builder, Default, DefaultEnvironment


env = DefaultEnvironment()
project_dir = Path(env.subst("$PROJECT_DIR"))
sdk_dir = project_dir / env.GetProjectOption("custom_sdk_dir")
sdk = sdk_dir / "sdk"
if not (sdk / "platform/include/da14531.h").is_file():
    sys.stderr.write(
        "DA14531 SDK not found. Unpack SDK_6.0.24.1464.zip in the project "
        "directory, or set custom_sdk_dir in platformio.ini.\n"
    )
    env.Exit(1)

env.Replace(
    AR="arm-none-eabi-gcc-ar",
    AS="arm-none-eabi-as",
    CC="arm-none-eabi-gcc",
    CXX="arm-none-eabi-g++",
    GDB="arm-none-eabi-gdb",
    OBJCOPY="arm-none-eabi-objcopy",
    RANLIB="arm-none-eabi-gcc-ranlib",
    SIZETOOL="arm-none-eabi-size",
    ARFLAGS=["rc"],
    PROGNAME="firmware",
    PROGSUFFIX=".elf",
    LDSCRIPT_PATH="$BUILD_DIR/ldscript_DA14531.lds",
    SIZEPROGREGEXP=r"^(?:ER_IROM1|ER_IROM2|ER_IROM3|\.ARM\.extab|\.ARM\.exidx|\.copy\.table|\.zero\.table|\.data)\s+(\d+).*",
    # Both code and data occupy SRAM on the DA14531.
    SIZEDATAREGEXP=r"^(?:ER_IROM1|ER_IROM2|ER_IROM3|\.ARM\.extab|\.ARM\.exidx|\.copy\.table|\.zero\.table|\.data|\.bss|ER_PRODTEST|ER_NZI|\.heap|\.stack_dummy)\s+(\d+).*",
    SIZECHECKCMD="$SIZETOOL -A -d $SOURCES",
    SIZEPRINTCMD="$SIZETOOL -B -d $SOURCES",
)

# Include paths, sources and flags follow the SDK's ble_app_noncon (Keil) and
# prox_reporter (Eclipse/GCC) DA14531 configurations.
include_dirs = [
    "sdk/app_modules/api",
    "sdk/platform/include/CMSIS/5.9.0/CMSIS/Core/Include",
    "sdk/ble_stack/controller/em",
    "sdk/ble_stack/controller/llc",
    "sdk/ble_stack/controller/lld",
    "sdk/ble_stack/controller/llm",
    "sdk/ble_stack/ea/api",
    "sdk/ble_stack/em/api",
    "sdk/ble_stack/hci/api",
    "sdk/ble_stack/hci/src",
    "sdk/ble_stack/host/att",
    "sdk/ble_stack/host/att/attc",
    "sdk/ble_stack/host/att/attm",
    "sdk/ble_stack/host/att/atts",
    "sdk/ble_stack/host/gap",
    "sdk/ble_stack/host/gap/gapc",
    "sdk/ble_stack/host/gap/gapm",
    "sdk/ble_stack/host/gatt",
    "sdk/ble_stack/host/gatt/gattc",
    "sdk/ble_stack/host/gatt/gattm",
    "sdk/ble_stack/host/l2c/l2cc",
    "sdk/ble_stack/host/l2c/l2cm",
    "sdk/ble_stack/host/smp",
    "sdk/ble_stack/host/smp/smpc",
    "sdk/ble_stack/host/smp/smpm",
    "sdk/ble_stack/profiles",
    "sdk/ble_stack/rwble",
    "sdk/ble_stack/rwble_hl",
    "sdk/common_project_files",
    "sdk/platform/arch",
    "sdk/platform/arch/boot",
    "sdk/platform/arch/compiler",
    "sdk/platform/arch/ll",
    "sdk/platform/arch/main",
    "sdk/platform/core_modules/arch_console",
    "sdk/platform/core_modules/common/api",
    "sdk/platform/core_modules/crypto",
    "sdk/platform/core_modules/dbg/api",
    "sdk/platform/core_modules/gtl/api",
    "sdk/platform/core_modules/gtl/src",
    "sdk/platform/core_modules/h4tl/api",
    "sdk/platform/core_modules/ke/api",
    "sdk/platform/core_modules/ke/src",
    "sdk/platform/core_modules/nvds/api",
    "sdk/platform/core_modules/rf/api",
    "sdk/platform/core_modules/rwip/api",
    "sdk/platform/driver/adc",
    "sdk/platform/driver/battery",
    "sdk/platform/driver/ble",
    "sdk/platform/driver/dma",
    "sdk/platform/driver/gpio",
    "sdk/platform/driver/hw_otpc",
    "sdk/platform/driver/i2c",
    "sdk/platform/driver/i2c_eeprom",
    "sdk/platform/driver/pdm",
    "sdk/platform/driver/reg",
    "sdk/platform/driver/rtc",
    "sdk/platform/driver/spi",
    "sdk/platform/driver/spi_flash",
    "sdk/platform/driver/syscntl",
    "sdk/platform/driver/systick",
    "sdk/platform/driver/timer",
    "sdk/platform/driver/trng",
    "sdk/platform/driver/uart",
    "sdk/platform/driver/wkupct_quadec",
    "sdk/platform/include",
    "sdk/platform/system_library/include",
    "sdk/platform/utilities/otp_cs",
    "sdk/platform/utilities/otp_hdr",
    "third_party/hash",
    "third_party/irng",
    "third_party/rand",
]
profiles_dir = sdk / "ble_stack/profiles"
profile_dirs = sorted(p for p in profiles_dir.iterdir() if p.is_dir())
profile_dirs += sorted(profiles_dir.glob("**/api"))

project_include = project_dir / "include"
forced_includes = []
for header in ("da1458x_config_basic.h", "da1458x_config_advanced.h", "user_config.h"):
    forced_includes += ["-include", str(project_include / header)]

cpu_flags = ["-mcpu=cortex-m0plus", "-mthumb"]
env.Append(
    ASFLAGS=cpu_flags,
    ASPPFLAGS=["-x", "assembler-with-cpp"],
    CCFLAGS=cpu_flags + forced_includes + [
        "-Os", "-g3", "-fsigned-char", "-ffunction-sections", "-fdata-sections",
        "-Wall", "-Wno-int-conversion", "-Wno-unused-variable",
    ],
    CFLAGS=["-std=gnu99"],
    CXXFLAGS=["-fno-exceptions", "-fno-rtti"],
    CPPDEFINES=["__DA14531__"],
    CPPPATH=[str(project_include)] + [str(sdk_dir / p) for p in include_dirs] + [str(p) for p in profile_dirs],
    LINKFLAGS=cpu_flags + [
        "--specs=nano.specs",
        "--specs=nosys.specs",
        "-Wl,--gc-sections",
        "-Wl,--no-wchar-size-warning",
        "-Wl,-Map,$BUILD_DIR/firmware.map",
    ],
    # da14531.lib: ROM patches and arch_asm (GNU archive of Keil objects).
    # misc/: da14531_symbols.lds, INCLUDEd by the linker script.
    LIBPATH=[
        str(sdk / "platform/system_library/output/Keil_5"),
        str(sdk / "common_project_files/misc"),
    ],
    LIBS=[":da14531.lib", "c", "m", "gcc"],
)

# SDK sources are read in place; all objects go under .pio/build/<environment>.
sdk_sources = [
    "sdk/platform/arch/boot/startup_DA14531.c",
    "sdk/platform/arch/boot/system_DA14531.c",
    "sdk/platform/arch/main/arch_hibernation.c",
    "sdk/platform/arch/main/arch_main.c",
    "sdk/platform/arch/main/arch_rom.c",
    "sdk/platform/arch/main/arch_sleep.c",
    "sdk/platform/arch/main/arch_system.c",
    "sdk/platform/arch/main/hardfault_handler.c",
    "sdk/platform/arch/main/jump_table.c",
    "sdk/platform/arch/main/nmi_handler.c",
    "sdk/platform/core_modules/arch_console/arch_console.c",
    "sdk/platform/core_modules/nvds/src/nvds.c",
    "sdk/platform/core_modules/rf/src/ble_arp.c",
    "sdk/platform/core_modules/rf/src/rf_531.c",
    "sdk/platform/core_modules/rwip/src/rwip.c",
    "sdk/platform/driver/adc/adc_531.c",
    "sdk/platform/driver/battery/battery.c",
    "sdk/platform/driver/dma/dma.c",
    "sdk/platform/driver/gpio/gpio.c",
    "sdk/platform/driver/hw_otpc/hw_otpc_531.c",
    "sdk/platform/driver/i2c/i2c.c",
    "sdk/platform/driver/i2c_eeprom/i2c_eeprom.c",
    "sdk/platform/driver/spi/spi_531.c",
    "sdk/platform/driver/spi_flash/spi_flash.c",
    "sdk/platform/driver/syscntl/syscntl.c",
    "sdk/platform/driver/trng/trng.c",
    "sdk/platform/driver/uart/uart.c",
    "sdk/platform/driver/wkupct_quadec/wkupct_quadec.c",
    "sdk/platform/system_library/src/DA14531/system_library_531.c",
    "sdk/platform/utilities/otp_cs/otp_cs.c",
    "sdk/platform/utilities/otp_hdr/otp_hdr.c",
    "sdk/ble_stack/rwble/rwble.c",
    "sdk/ble_stack/profiles/prf.c",
    "sdk/ble_stack/profiles/prf_utils.c",
    "sdk/ble_stack/profiles/custom/custom_common.c",
    "sdk/ble_stack/profiles/custom/custs/src/custs1.c",
    "sdk/ble_stack/profiles/custom/custs/src/custs1_task.c",
    "sdk/ble_stack/profiles/dis/diss/src/diss.c",
    "sdk/ble_stack/profiles/dis/diss/src/diss_task.c",
    "sdk/app_modules/src/app_common/app.c",
    "sdk/app_modules/src/app_common/app_msg_utils.c",
    "sdk/app_modules/src/app_common/app_task.c",
    "sdk/app_modules/src/app_common/app_utils.c",
    "sdk/app_modules/src/app_custs/app_customs.c",
    "sdk/app_modules/src/app_custs/app_customs_common.c",
    "sdk/app_modules/src/app_custs/app_customs_task.c",
    "sdk/app_modules/src/app_default_hnd/app_default_handlers.c",
    "sdk/app_modules/src/app_diss/app_diss.c",
    "sdk/app_modules/src/app_diss/app_diss_task.c",
    "sdk/app_modules/src/app_easy/app_easy_msg_utils.c",
    "sdk/app_modules/src/app_easy/app_easy_security.c",
    "sdk/app_modules/src/app_easy/app_easy_timer.c",
    "sdk/app_modules/src/app_easy/app_easy_whitelist.c",
    "sdk/app_modules/src/app_entry/app_entry_point.c",
    "sdk/app_modules/src/app_sec/app_security.c",
    "sdk/app_modules/src/app_sec/app_security_task.c",
    "third_party/hash/hash.c",
    "third_party/rand/chacha20.c",
]
env.BuildSources(
    "$BUILD_DIR/sdk",
    str(sdk_dir),
    src_filter=["-<*>"] + ["+<%s>" % p for p in sdk_sources],
)

# GNU LD script generated from the SDK template with this project's config.
ldscript_dir = sdk / "common_project_files/ldscripts"


def align_stack_top(target, source, env):
    # AAPCS requires an 8-byte aligned initial SP. The SDK template places the
    # stack directly after the 4-byte aligned heap, so __StackTop alignment
    # would otherwise depend on the .bss size.
    path = Path(str(target[0]))
    text = path.read_text()
    symbol = "__StackTop = .;"
    if text.count(symbol) != 1:
        raise ValueError("Unexpected SDK linker script: expected one '%s'" % symbol)
    path.write_text(text.replace(symbol, ". = ALIGN(8);\n        " + symbol))


ldscript = env.Command(
    "$BUILD_DIR/ldscript_DA14531.lds",
    str(ldscript_dir / "ldscript_DA14531.lds.S"),
    [
        env.VerboseAction(
            "$CC -E -P -c -D__DA14531__ -I\"%s\" -I\"%s\" -I\"%s\" $SOURCE -o $TARGET" % (
                project_include, sdk / "common_project_files", ldscript_dir),
            "Generating linker script $TARGET",
        ),
        env.VerboseAction(align_stack_top, "Aligning __StackTop to 8 bytes"),
    ],
)
env.Depends(ldscript, [str(project_include / h) for h in (
    "da1458x_config_basic.h", "da1458x_config_advanced.h", "user_profiles_config.h")])


def make_boot_image(source, target, env):
    data = Path(str(source[0])).read_bytes()
    if not 8 <= len(data) <= 40960:
        raise ValueError("DA14531 application image must be between 8 and 40960 bytes")
    # AN-B-001 SPI boot header: 70 50, four zero bytes, big-endian image length.
    Path(str(target[0])).write_bytes(b"\x70\x50\0\0\0\0" + struct.pack(">H", len(data)) + data)


env.Append(BUILDERS={
    "ElfToBin": Builder(
        action=env.VerboseAction("$OBJCOPY -O binary $SOURCE $TARGET", "Building $TARGET"),
        suffix=".bin",
    ),
    "BootImage": Builder(
        action=env.VerboseAction(make_boot_image, "Building SPI boot image $TARGET"),
        suffix=".boot.bin",
    ),
})

firmware_elf = env.BuildProgram()
firmware_bin = env.ElfToBin("$BUILD_DIR/${PROGNAME}", firmware_elf)
boot_image = env.BootImage("$BUILD_DIR/${PROGNAME}", firmware_bin)
env.Depends(firmware_bin, "checkprogsize")
env.Depends(firmware_elf, ldscript)
build = env.Alias("buildprog", [firmware_bin, boot_image])
size = env.Alias("size", firmware_elf, env.VerboseAction("$SIZEPRINTCMD", "Calculating size $SOURCE"))
AlwaysBuild(size)


def upload(source, target, env):
    openocd_dir = Path(env.PioPlatform().get_package_dir("tool-openocd"))
    executable = "openocd.exe" if sys.platform == "win32" else "openocd"
    command = [
        sys.executable, str(project_dir / "scripts/upload.py"),
        "--mode", env.GetProjectOption("custom_upload_mode"),
        "--firmware", str(firmware_bin[0]),
        "--boot-image", str(boot_image[0]),
        "--sdk", str(sdk_dir),
        "--openocd", str(openocd_dir / "bin" / executable),
        "--scripts", str(openocd_dir / "openocd/scripts"),
        "--config", str(project_dir / "scripts" / "da14531-dap.cfg"),
        "--speed", env.GetProjectOption("custom_swd_speed"),
        "--otp-confirm", env.GetProjectOption("custom_otp_confirm", ""),
    ]
    for pin in ("cs", "clk", "mosi", "miso"):
        command += ["--" + pin, env.GetProjectOption("custom_flash_" + pin)]
    return subprocess.call(command)


upload_target = env.Alias(
    "upload", [firmware_bin, boot_image],
    env.VerboseAction(upload, "Uploading DA14531 firmware via CMSIS-DAP"),
)
AlwaysBuild(upload_target)
Default(build, size)
