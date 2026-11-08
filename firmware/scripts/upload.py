"""DA14531 SRAM/SPI Flash/OTP uploads using OpenOCD and the SDK's SWD programmer."""

import argparse
import struct
import subprocess
import sys
from pathlib import Path


RAM_BASE = 0x07FC0000
APP_RAM_SIZE = 40 * 1024
BUFFER = 0x07FC4000
CHUNK_SIZE = 8192
SECTOR_SIZE = 4096
PAGE_SIZE = 256
# Conservative quiet intervals for this board's W25X20CL. SWD accesses while
# erase/program is busy produced parity errors with the attached CMSIS-DAP.
ERASE_QUIET_MS = 500
PAGE_PROGRAM_QUIET_MS = 20

# OTP layout: SDK otp_hdr.h, otp_cs.h and toolbox_resources/DA1453x/common/header.txt.
OTP_CODE_LIMIT = 0x7ED0          # the factory configuration script starts here
OTP_HDR_APP_FLAGS = 0x7FC0
OTP_HDR_DMA_LENGTH = 0x7FDC      # number of 32-bit words mirrored to SysRAM
OTP_APP_FLAGS = (0x1234A5A5, 0xA5A51234)
OTP_CONFIRM = "i_understand_otp_is_permanent"

# Protocol and addresses: utilities/flash_programmer/src/programmer.c.
# 0x07FC3C00: action (u8), reserved (u8), size (u16), address (u32),
#             result (u32), optional result value (u32).
TCL_HELPERS = """
proc upload_step {label command} {
    echo "DA14531: $label"
    if {[catch {uplevel 1 $command} result]} {
        error "DA14531: $label failed: $result"
    }
    return $result
}

proc peek32 {address} {
    return [lindex [read_memory $address 32 1] 0]
}

proc programmer_status {} {
    foreach {name address} {
        RESET_STAT 0x50000304
        HWR_CTRL 0x50000300
        POR_PIN 0x50000040
        WATCHDOG_CTRL 0x50003102
        WATCHDOG 0x50003100
        ANA_STATUS 0x5000002a
    } {
        if {[catch {read_memory $address 16 1} values]} {
            echo "DA14531: $name unavailable"
        } else {
            echo [format "DA14531: %s=0x%04x" $name [lindex $values 0]]
        }
    }
}

proc wait_programmer_ready {attempts} {
    # The SDK writes ACTION_READY with a uint8_t store, leaving the upper
    # three bytes unchanged. Command responses later use uint32_t stores.
    for {set i 0} {$i < $attempts} {incr i} {
        set ready [lindex [read_memory 0x07fc3c08 8 1] 0]
        if {$ready == 0x5a} {
            echo "DA14531: SDK programmer ready (0x5a)"
            return
        }
        sleep 10
    }
    error [format "SDK programmer did not become ready: mailbox=0x%08x" [peek32 0x07fc3c08]]
}

proc wait_result {attempts} {
    for {set i 0} {$i < $attempts} {incr i} {
        set result [peek32 0x07fc3c08]
        if {$result != 0xffffffff} {
            return $result
        }
        sleep 10
    }
    error "SDK programmer timed out waiting for a result"
}

proc sdk_command {action address size expected {quiet_ms 0}} {
    echo [format "DA14531: SDK action 0x%02x, address=0x%x, size=%d" $action $address $size]
    mww 0x07fc3c08 0xffffffff
    mww 0x07fc3c0c 0
    mww 0x07fc3c04 $address
    mwh 0x07fc3c02 $size
    # Set the action last, after its parameters and buffer are ready.
    mwb 0x07fc3c00 $action
    if {$quiet_ms > 0} {
        # Background polling must also be off: sleep alone permits OpenOCD
        # to access DHCSR while the SDK is performing the Flash operation.
        echo "DA14531: Flash busy interval: no SWD accesses for $quiet_ms ms"
        sleep $quiet_ms
    }
    if {[catch {wait_result 1000} result]} {
        programmer_status
        error [format "SDK action 0x%02x failed while waiting: %s" $action $result]
    }
    if {$result != $expected} {
        set detail [peek32 0x07fc3c0c]
        programmer_status
        error [format "SDK action 0x%02x failed: result=0x%02x detail=0x%08x" $action $result $detail]
    }
}

proc prepare_cpu {} {
    global da14531_swdio_pin
    set sys_ctrl [lindex [upload_step "Read SYS_CTRL_REG" {read_memory 0x50000012 16 1}] 0]
    set swd_route [expr {($sys_ctrl >> 7) & 3}]
    set da14531_swdio_pin [lindex {-1 5 1 10} $swd_route]
    echo [format "DA14531: SYS_CTRL_REG=0x%04x, SWD route=%d, SWDIO=P0_%d" $sys_ctrl $swd_route $da14531_swdio_pin]
    if {$da14531_swdio_pin < 0} {
        error "No active SWD route; reset or power-cycle the target and reconnect during the startup window"
    }
    set dhcsr [upload_step "Read DHCSR" {peek32 0xe000edf0}]
    echo [format "DA14531: DHCSR=0x%08x" $dhcsr]
    upload_step "Halt CPU" {halt}
    upload_step "Wait for CPU halt" {wait_halt 2000}
    # Freeze watchdog, stop SysTick, disable and clear all external IRQs.
    upload_step "Freeze watchdog" {mwh 0x50003300 0x0008}
    upload_step "Stop SysTick" {mww 0xe000e010 0}
    upload_step "Disable external IRQs" {mww 0xe000e180 0xffffffff}
    upload_step "Clear external IRQs" {mww 0xe000e280 0xffffffff}
    upload_step "Clear pending system exceptions" {mww 0xe000ed04 0x0a000000}
    # Keep DEBUGGER_ENABLE unchanged: switching a live route disconnects SWD.
    set sys_ctrl [lindex [read_memory 0x50000012 16 1] 0]
    upload_step "Select RAM1 remap (keep SWDIO=P0_$da14531_swdio_pin)" {
        mwh 0x50000012 [expr {($sys_ctrl & ~0x8013) | 0x0002}]
    }
}

proc start_image {stack entry {detach 0}} {
    # The SDK's jlink_DA14531.ini resets after setting REMAP_ADR0. A direct
    # branch alone leaves exception vectors mapped to ROM instead of RAM.
    upload_step "Apply RAM1 remap with SYSRESETREQ" {reset halt}
    upload_step "Freeze watchdog after reset" {mwh 0x50003300 0x0008}
    echo "DA14531: Set initial CPU registers and run image"
    reg control 0
    reg msp $stack
    reg sp $stack
    reg primask 1
    reg xpsr 0x01000000
    if {$detach} {
        echo "DA14531: Stop polling before the application enters extended sleep (SWD off)"
        poll off
    }
    resume [expr {$entry & ~1}]
}

# OTP offsets are relative to 0x07F80000 and blank words read 0xffffffff.
# otp_utils_write_data() skips 0xffffffff words but reports failure when a
# command starts with one, so each write covers only words still to program.
proc otp_read {offset count} {
    sdk_command 0x80 $offset [expr {$count * 4}] 0x82
    return [read_memory 0x07fc4000 32 $count]
}

# Returns {offset words} writes for blank words. A word holding another value
# cannot be changed and aborts before anything is written.
proc otp_plan {offset words} {
    set count [llength $words]
    set current [otp_read $offset $count]
    set writes {}
    set start -1
    for {set i 0} {$i <= $count} {incr i} {
        set needed 0
        if {$i < $count && [lindex $current $i] != [lindex $words $i]} {
            if {[lindex $current $i] != 0xffffffff} {
                error [format "OTP 0x%04x holds 0x%08x, expected 0x%08x or blank; OTP was not written" [expr {$offset + $i * 4}] [lindex $current $i] [lindex $words $i]]
            }
            set needed 1
        }
        # 256 words (1 KiB) per command finish well within wait_result's 10 s.
        if {$start >= 0 && (!$needed || $i - $start == 256)} {
            lappend writes [list [expr {$offset + $start * 4}] [lrange $words $start [expr {$i - 1}]]]
            set start -1
        }
        if {$needed && $start < 0} {
            set start $i
        }
    }
    return $writes
}

# Checks every region before the first write; regions are written and read
# back in order, so the header that enables OTP boot comes last.
proc otp_program {regions write} {
    set plans {}
    set total 0
    foreach region $regions {
        set writes [otp_plan [lindex $region 0] [lindex $region 1]]
        lappend plans $writes
        foreach item $writes {
            incr total [llength [lindex $item 1]]
        }
    }
    if {$total == 0} {
        echo "DA14531: OTP already holds this image and header; nothing to write"
        return
    }
    echo "DA14531: OTP check passed; $total words to program"
    if {!$write} {
        echo "DA14531: Dry run; OTP not written"
        return
    }
    foreach region $regions writes $plans {
        if {![llength $writes]} {
            continue
        }
        foreach item $writes {
            set data [lindex $item 1]
            write_memory 0x07fc4000 32 $data
            sdk_command 0x81 [lindex $item 0] [expr {[llength $data] * 4}] 0x83
        }
        set offset [lindex $region 0]
        set words [lindex $region 1]
        set current [otp_read $offset [llength $words]]
        for {set i 0} {$i < [llength $words]} {incr i} {
            if {[lindex $current $i] != [lindex $words $i]} {
                error [format "OTP verify failed at 0x%04x: 0x%08x, expected 0x%08x" [expr {$offset + $i * 4}] [lindex $current $i] [lindex $words $i]]
            }
        }
        echo [format "DA14531: OTP 0x%04x-0x%04x written and verified" $offset [expr {$offset + [llength $words] * 4 - 1}]]
    }
}
"""


def tcl_string(value):
    value = str(value).replace("\\", "/")
    for character in ('"', "$", "[", "]"):
        value = value.replace(character, "\\" + character)
    return '"' + value.replace("\n", "\\n").replace("\r", "\\r") + '"'


def image_vectors(data, limit):
    if not 8 <= len(data) <= limit:
        raise ValueError("Invalid DA14531 binary length: %d" % len(data))
    stack, entry = struct.unpack_from("<II", data)
    if not RAM_BASE < stack <= RAM_BASE + limit or stack % 8:
        raise ValueError("Image has an invalid initial stack pointer")
    if not entry & 1 or not RAM_BASE <= (entry & ~1) < RAM_BASE + len(data):
        raise ValueError("Image has an invalid Thumb reset vector")
    return stack, entry


def programmer_commands(args, checks=()):
    programmer = args.sdk / "config/toolbox_resources/DA1453x/common/jtag_programmer_531.bin"
    stack, entry = image_vectors(programmer.read_bytes(), 0x3C00)
    return [
        "prepare_cpu",
        *checks,
        "mww 0x07fc3c08 0xffffffff",
        "load_image %s 0x%08x bin" % (tcl_string(programmer), RAM_BASE),
        "verify_image %s 0x%08x bin" % (tcl_string(programmer), RAM_BASE),
        "start_image 0x%08x 0x%08x" % (stack, entry),
        "wait_programmer_ready 1000",
        # The mailbox handles completion; no background CPU polling is needed.
        "poll off",
    ]


def otp_commands(args, data, write):
    # The ROM mirrors OTP_DMA_LENGTH words from OTP offset 0 to SysRAM
    # 0x07FC0000 when both flags are set, so the RAM-linked image is used as is.
    if len(data) > OTP_CODE_LIMIT:
        raise ValueError("Image is %d bytes; OTP holds at most %d bytes before its configuration script"
                         % (len(data), OTP_CODE_LIMIT))
    padded = data + b"\xff" * (-len(data) % 4)
    words = struct.unpack("<%dI" % (len(padded) // 4), padded)
    step = CHUNK_SIZE // 4
    regions = [(index * 4, words[index:index + step]) for index in range(0, len(words), step)]
    regions += [(OTP_HDR_DMA_LENGTH, (len(words),)), (OTP_HDR_APP_FLAGS, OTP_APP_FLAGS)]
    tcl_regions = " ".join(
        "{0x%04x {%s}}" % (offset, " ".join("0x%08x" % word for word in region))
        for offset, region in regions)

    commands = programmer_commands(args) + [
        'echo "DA14531: OTP image %d bytes, OTP_DMA_LENGTH=%d words"' % (len(data), len(words)),
        "otp_program {%s} %d" % (tcl_regions, int(write)),
    ]
    if write:
        commands.append('echo "OTP written and read-back verified; the ROM now boots from OTP, not SPI Flash"')
    commands += [
        'echo "DA14531: SWD stays available for 10 s after reset; extended sleep then disables it"',
        "poll off",
        "mwb 0x07fc3c00 0xc0",
    ]
    return commands


def flash_commands(args, boot_data, work_dir):
    pins = [args.cs, args.clk, args.mosi, args.miso]
    if len(set(pins)) != 4 or any(pin not in range(12) or pin == 2 for pin in pins):
        raise ValueError("SPI needs four distinct P0 pins, excluding SWCLK=P0_2")

    commands = programmer_commands(args, [
        'if {[lsearch -exact {%s} $da14531_swdio_pin] >= 0} {error "SPI pin assignment conflicts with the active SWDIO pin"}' % " ".join(map(str, pins)),
    ])

    if 0 in pins:
        # ACTION_GPIOS calls check_gpio_hw_reset(P0_0), which marks P0_0 as
        # claimed and disables both HW reset and GPIO POR. This prevents
        # set_pad_spi() from re-enabling a POR timer on the SPI data pin.
        # GPIO record: port=0, pin=0, cfg=INPUT (uint16_t, little-endian).
        commands += [
            'echo "DA14531: Reserve P0_0 for SPI and disable its reset functions"',
            "write_memory 0x%08x 8 {0 0 0 0}" % BUFFER,
            "sdk_command 0x32 0 4 0x83",
            "programmer_status",
            'if {[lindex [read_memory 0x50000300 16 1] 0] != 1 || [lindex [read_memory 0x50000040 16 1] 0] != 0} {error "P0_0 reset functions are still enabled; Flash was not erased"}',
        ]

    # ACTION_SPI_GPIOS: CS, CLK, DO (MOSI), DI (MISO), then no power-enable pin.
    pin_bytes = [0, args.cs, 0, args.clk, 0, args.mosi, 0, args.miso, 0, 0, 0]
    commands += [
        "write_memory 0x%08x 8 {%s}" % (BUFFER, " ".join(map(str, pin_bytes))),
        "sdk_command 0x95 0 0 0x83",
        "sdk_command 0x93 0 0 0x82",
        "set jedec [peek32 0x07fc3c0c]",
        # This board has a W25X20CL. Refuse to erase an unexpected device.
        'if {$jedec != 0xef3012} {error [format "Expected W25X20CL (EF3012), got %06x; Flash was not erased" $jedec]}',
        'echo [format "SPI JEDEC ID: %06x" $jedec]',
    ]

    sectors = (len(boot_data) + SECTOR_SIZE - 1) // SECTOR_SIZE
    commands += [
        'echo "Erasing %d SPI sectors at offset 0"' % sectors,
        "sdk_command 0x94 0 %d 0x83 %d" % (sectors, sectors * ERASE_QUIET_MS),
    ]
    for offset in range(0, len(boot_data), CHUNK_SIZE):
        chunk = boot_data[offset:offset + CHUNK_SIZE]
        chunk_path = work_dir / ("chunk-%04x.bin" % offset)
        chunk_path.write_bytes(chunk)
        pages = (offset % PAGE_SIZE + len(chunk) + PAGE_SIZE - 1) // PAGE_SIZE
        commands += [
            "load_image %s 0x%08x bin" % (tcl_string(chunk_path), BUFFER),
            "sdk_command 0x91 %d %d 0x83 %d" % (
                offset, len(chunk), 50 + pages * PAGE_PROGRAM_QUIET_MS),
            "sdk_command 0x90 %d %d 0x82" % (offset, len(chunk)),
            # OpenOCD's RAM checksum algorithm requires a halted CPU. Pause
            # only after the SDK has finished the readback, then restore it.
            "poll on",
            "halt",
            "wait_halt 2000",
            "verify_image %s 0x%08x bin" % (tcl_string(chunk_path), BUFFER),
            "resume",
            "poll off",
        ]

    commands += [
        'echo "SPI Flash written and read-back verified; restarting through ROM"',
        'echo "DA14531: SWD stays available for 10 s after reset; extended sleep then disables it"',
        # ACTION_PLATFORM_RESET uses the SDK's watchdog reset, returning to ROM.
        # Stop polling before reset; the application later disables SWD by sleeping.
        "poll off",
        "mwb 0x07fc3c00 0xc0",
    ]
    return commands


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", choices=("flash", "sram", "otp"), required=True)
    for option in ("firmware", "boot-image", "sdk", "openocd", "scripts", "config"):
        parser.add_argument("--" + option, type=Path, required=True)
    parser.add_argument("--speed", type=int, default=100)
    parser.add_argument("--cs", type=int, default=1)
    parser.add_argument("--clk", type=int, default=4)
    parser.add_argument("--mosi", type=int, default=0)
    parser.add_argument("--miso", type=int, default=3)
    parser.add_argument("--otp-confirm", default="")
    args = parser.parse_args()

    for option in ("firmware", "boot_image", "sdk", "openocd", "scripts", "config"):
        setattr(args, option, getattr(args, option).resolve())
    data = args.firmware.read_bytes()
    stack, entry = image_vectors(data, APP_RAM_SIZE)
    if args.speed <= 0:
        raise ValueError("SWD speed must be greater than zero")

    work_dir = args.firmware.parent / "upload"
    work_dir.mkdir(exist_ok=True)
    otp_write = args.otp_confirm == OTP_CONFIRM
    commands = [TCL_HELPERS, 'upload_step "Initialize SWD target" {init}']
    if args.mode == "sram":
        commands += [
            "prepare_cpu",
            "load_image %s 0x%08x bin" % (tcl_string(args.firmware), RAM_BASE),
            "verify_image %s 0x%08x bin" % (tcl_string(args.firmware), RAM_BASE),
            "start_image 0x%08x 0x%08x 1" % (stack, entry),
            'echo "RAM image verified and started; this image is lost on power-off"',
        ]
    elif args.mode == "otp":
        commands += otp_commands(args, data, otp_write)
    else:
        boot_data = args.boot_image.read_bytes()
        expected = b"\x70\x50\0\0\0\0" + struct.pack(">H", len(data)) + data
        if boot_data != expected:
            raise ValueError("SPI boot image does not match firmware.bin; rebuild before uploading")
        commands += flash_commands(args, boot_data, work_dir)
    commands.append("shutdown")
    script = work_dir / "upload.tcl"
    script.write_text("\n".join(commands) + "\n", encoding="utf-8")
    command = [
        str(args.openocd), "-s", str(args.scripts),
        "-c", "gdb_port disabled; tcl_port disabled; telnet_port disabled",
        "-f", str(args.config), "-c", "adapter speed %d" % args.speed,
        "-f", str(script),
    ]
    log_path = work_dir / "openocd.log"
    print("DA14531: OpenOCD log: %s" % log_path, flush=True)
    with log_path.open("w", encoding="utf-8") as log_file:
        with subprocess.Popen(
            command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, errors="replace",
        ) as process:
            for line in process.stdout:
                print(line, end="", flush=True)
                log_file.write(line)
            status = process.wait()
    if status == 0 and args.mode == "otp" and not otp_write:
        print("DA14531 upload: dry run only, OTP not written. Set custom_otp_confirm = %s "
              "to burn it permanently." % OTP_CONFIRM, file=sys.stderr)
        return 1
    return status


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError) as error:
        print("DA14531 upload: %s" % error, file=sys.stderr)
        sys.exit(1)
