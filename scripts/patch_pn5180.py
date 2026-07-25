Import("env")

from pathlib import Path


PINNED_PN5180_COMMIT = "b7fa129c62173b3561bc80ab2a105be02163522d"
PATCH_MARKER = "// blockwords: propagate PN5180 transport failures"


def replace_once(source, old, new, path):
    if new in source:
        return source
    if old not in source:
        raise RuntimeError(f"PN5180 patch context not found in {path}")
    return source.replace(old, new, 1)


def replace_expected(source, old, new, count, path):
    if source.count(new) == count:
        return source
    if source.count(old) != count:
        raise RuntimeError(f"Expected {count} PN5180 patch contexts in {path}")
    return source.replace(old, new)


def patch_core(path):
    source = path.read_text()
    if PATCH_MARKER in source:
        return

    source = replace_once(
        source,
        '#include "Debug.h"\n',
        f'#include "Debug.h"\n\n{PATCH_MARKER}\n',
        path,
    )

    source = replace_expected(
        source,
        "  transceiveCommand(buf, 6);\n"
        "  PN5180_SPI.endTransaction();\n\n"
        "  return true;",
        "  bool success = transceiveCommand(buf, 6);\n"
        "  PN5180_SPI.endTransaction();\n\n"
        "  return success;",
        3,
        path,
    )
    source = replace_once(
        source,
        "  transceiveCommand(cmd, 3);\n"
        "  PN5180_SPI.endTransaction();\n\n"
        "  return true;",
        "  bool success = transceiveCommand(cmd, 3);\n"
        "  PN5180_SPI.endTransaction();\n\n"
        "  return success;",
        path,
    )

    source = replace_once(
        source,
        "  PN5180_SPI.beginTransaction(SPI_SETTINGS);\n"
        "  transceiveCommand(cmd, 2, (uint8_t*)value, 4);\n"
        "  PN5180_SPI.endTransaction();\n\n"
        "#ifdef DEBUG",
        "  *value = 0;\n"
        "  PN5180_SPI.beginTransaction(SPI_SETTINGS);\n"
        "  bool success = transceiveCommand(cmd, 2, (uint8_t*)value, 4);\n"
        "  PN5180_SPI.endTransaction();\n"
        "  if (!success) return false;\n\n"
        "#ifdef DEBUG",
        path,
    )

    source = replace_once(
        source,
        "  writeRegisterWithAndMask(SYSTEM_CONFIG, 0xfffffff8);  // Idle/StopCom Command\n"
        "  writeRegisterWithOrMask(SYSTEM_CONFIG, 0x00000003);   // Transceive Command",
        "  if (!writeRegisterWithAndMask(SYSTEM_CONFIG, 0xfffffff8) ||\n"
        "      !writeRegisterWithOrMask(SYSTEM_CONFIG, 0x00000003)) {\n"
        "    return false;\n"
        "  }",
        path,
    )

    source = replace_once(
        source,
        "  PN5180_SPI.beginTransaction(SPI_SETTINGS);\n"
        "  transceiveCommand(cmd, 2, readBuffer, len);\n"
        "  PN5180_SPI.endTransaction();\n\n"
        "  return readBuffer;",
        "  PN5180_SPI.beginTransaction(SPI_SETTINGS);\n"
        "  bool success = transceiveCommand(cmd, 2, readBuffer, len);\n"
        "  PN5180_SPI.endTransaction();\n\n"
        "  return success ? readBuffer : nullptr;",
        path,
    )

    source = replace_expected(
        source,
        "  PN5180_SPI.beginTransaction(SPI_SETTINGS);\n"
        "  transceiveCommand(cmd, 2);\n"
        "  PN5180_SPI.endTransaction();\n\n"
        "  unsigned long startedWaiting = millis();",
        "  PN5180_SPI.beginTransaction(SPI_SETTINGS);\n"
        "  bool success = transceiveCommand(cmd, 2);\n"
        "  PN5180_SPI.endTransaction();\n"
        "  if (!success) return false;\n\n"
        "  unsigned long startedWaiting = millis();",
        2,
        path,
    )
    source = replace_once(
        source,
        "  unsigned long startedWaiting = millis();\n"
        "  while (0 == (TX_RFON_IRQ_STAT & getIRQStatus())) {   // wait for RF field to set up (max 500ms)\n"
        "    if (millis() - startedWaiting > 500) {",
        "  unsigned long startedWaiting = millis();\n"
        "  uint32_t irqStatus = 0;\n"
        "  while (true) {   // wait for RF field to set up (max 500ms)\n"
        "    if (!readRegister(IRQ_STATUS, &irqStatus)) return false;\n"
        "    if (TX_RFON_IRQ_STAT & irqStatus) break;\n"
        "    if (millis() - startedWaiting > 500) {",
        path,
    )
    source = replace_once(
        source,
        "  unsigned long startedWaiting = millis();\n"
        "  while (0 == (TX_RFOFF_IRQ_STAT & getIRQStatus())) {   // wait for RF field to shut down\n"
        "    if (millis() - startedWaiting > 500) {",
        "  unsigned long startedWaiting = millis();\n"
        "  uint32_t irqStatus = 0;\n"
        "  while (true) {   // wait for RF field to shut down\n"
        "    if (!readRegister(IRQ_STATUS, &irqStatus)) return false;\n"
        "    if (TX_RFOFF_IRQ_STAT & irqStatus) break;\n"
        "    if (millis() - startedWaiting > 500) {",
        path,
    )

    start = source.index("bool PN5180::transceiveCommand(")
    end = source.index("\n}\n\n/*\n * Reset NFC device", start)
    function = source[start:end]
    function = function.replace(
        "\t\treturn false;",
        "\t\tdigitalWrite(PN5180_NSS, HIGH);\n\t\treturn false;",
    )
    if function.count("digitalWrite(PN5180_NSS, HIGH);\n\t\treturn false;") != 5:
        raise RuntimeError("Expected five PN5180 BUSY timeout exits")
    source = source[:start] + function + source[end:]

    source = replace_once(
        source,
        "uint32_t PN5180::getIRQStatus() {\n"
        "  uint32_t irqStatus;\n"
        "  readRegister(IRQ_STATUS, &irqStatus);\n"
        "  return irqStatus;\n"
        "}",
        "uint32_t PN5180::getIRQStatus() {\n"
        "  uint32_t irqStatus = 0;\n"
        "  readRegister(IRQ_STATUS, &irqStatus);\n"
        "  return irqStatus;\n"
        "}",
        path,
    )

    path.write_text(source)


def patch_iso15693(path):
    source = path.read_text()
    if PATCH_MARKER in source:
        return

    source = replace_once(
        source,
        '#include "Debug.h"\n',
        f'#include "Debug.h"\n\n{PATCH_MARKER}\n',
        path,
    )
    source = replace_once(
        source,
        "  sendData(cmd, cmdLen);\n"
        "  delay(10);\n\n"
        "  uint32_t irqR = getIRQStatus();",
        "  if (!sendData(cmd, cmdLen)) {\n"
        "    return ISO15693_EC_UNKNOWN_ERROR;\n"
        "  }\n"
        "  delay(10);\n\n"
        "  uint32_t irqR = 0;\n"
        "  if (!readRegister(IRQ_STATUS, &irqR)) {\n"
        "    return ISO15693_EC_UNKNOWN_ERROR;\n"
        "  }",
        path,
    )
    source = replace_once(
        source,
        "  while (!(irqR & RX_IRQ_STAT)) {\n"
        "    irqR = getIRQStatus();\n"
        "    if (millis() - startedWaiting > commandTimeout) {",
        "  while (!(irqR & RX_IRQ_STAT)) {\n"
        "    if (!readRegister(IRQ_STATUS, &irqR)) {\n"
        "      return ISO15693_EC_UNKNOWN_ERROR;\n"
        "    }\n"
        "    if (millis() - startedWaiting > commandTimeout) {",
        path,
    )
    source = replace_once(
        source,
        "  uint32_t rxStatus;\n"
        "  readRegister(RX_STATUS, &rxStatus);",
        "  uint32_t rxStatus = 0;\n"
        "  if (!readRegister(RX_STATUS, &rxStatus)) {\n"
        "    return ISO15693_EC_UNKNOWN_ERROR;\n"
        "  }",
        path,
    )
    source = replace_once(
        source,
        "  uint32_t irqStatus = getIRQStatus();\n"
        "  if (0 == (RX_SOF_DET_IRQ_STAT & irqStatus)) {",
        "  uint32_t irqStatus = 0;\n"
        "  if (!readRegister(IRQ_STATUS, &irqStatus)) {\n"
        "    return ISO15693_EC_UNKNOWN_ERROR;\n"
        "  }\n"
        "  if (0 == (RX_SOF_DET_IRQ_STAT & irqStatus)) {",
        path,
    )
    source = replace_once(
        source,
        "  writeRegisterWithAndMask(SYSTEM_CONFIG, 0xfffffff8);  // Idle/StopCom Command\n"
        "  writeRegisterWithOrMask(SYSTEM_CONFIG, 0x00000003);   // Transceive Command\n\n"
        "  return true;",
        "  if (!writeRegisterWithAndMask(SYSTEM_CONFIG, 0xfffffff8) ||\n"
        "      !writeRegisterWithOrMask(SYSTEM_CONFIG, 0x00000003)) {\n"
        "    return false;\n"
        "  }\n\n"
        "  return true;",
        path,
    )
    path.write_text(source)


libdeps_dir = Path(env.subst("$PROJECT_LIBDEPS_DIR"))
library_dir = libdeps_dir / env.subst("$PIOENV") / "PN5180-Library"
head_file = library_dir / ".git" / "HEAD"
if head_file.exists():
    git_dir = library_dir / ".git"
    import subprocess

    head = subprocess.check_output(
        ["git", "-C", str(library_dir), "rev-parse", "HEAD"], text=True
    ).strip()
    if head != PINNED_PN5180_COMMIT:
        raise RuntimeError(f"Unexpected PN5180 commit {head}")

patch_core(library_dir / "PN5180.cpp")
patch_iso15693(library_dir / "PN5180ISO15693.cpp")
