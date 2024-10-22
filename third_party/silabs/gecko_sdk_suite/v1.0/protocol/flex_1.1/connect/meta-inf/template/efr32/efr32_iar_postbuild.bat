@echo off

REM This file was generated by Ember Desktop from the following template:
REM   connet/meta-inf/template/efr32/efr32_iar_postbuild.bat
REM Please do not edit it directly.

REM Post Build processing for IAR Workbench.

REM Turn on delayed expansion
setlocal enabledelayedexpansion enableextensions

SET TARGET_BPATH=%1%
SET PROJECT_DIR=%2%
SET PROJECT_DIR=%PROJECT_DIR:"=%

IF "$--bootloader--$" == "NULL_BTL" (
  GOTO Done
)

REM Extracting the current directory
SET CURRENT_DIR=%cd:Z:=%
SET CURRENT_DIR=%CURRENT_DIR:\=/%

REM Extracting the path to s37 for iar and studio
SET TARGET_BPATH=%TARGET_BPATH:Z:=%
SET TARGET_BPATH=%TARGET_BPATH:\=/%
IF EXIST "%CURRENT_DIR%/%TARGET_BPATH%.s37" (
  SET TARGET_BPATH=%CURRENT_DIR%/%TARGET_BPATH%
)

SET ARCHITECTURE_SERIES=$--partHardware.series--$
SET ARCHITECTURE_CONFIGURATION=$--partHardware.device_configuration--$

IF [%ARCHITECTURE_SERIES%] GEQ [2] (
  GOTO GBL
)
IF [%ARCHITECTURE_CONFIGURATION%] GEQ [2] (
  GOTO GBL
)
GOTO EBL

:EBL
  SET CONVERT_FLAGS=Z:$--em3xxConvertFlags--$
  SET CONVERT_FLAGS=%CONVERT_FLAGS:Z:=%
  IF ["$--em3xxConvertFlags--$"] == [""] (
    SET EBL_FILE=%TARGET_BPATH%.ebl
  ) ELSE (
    SET CONVERT_FLAGS=%CONVERT_FLAGS:\=/%
    SET EBL_FILE=%TARGET_BPATH%.ebl.encrypted
  )
  echo " "
  echo "This converts S37 to Ember Bootload File format if a bootloader has been selected in AppBuilder"
  echo " "
  @echo on
  cmd /C "$--osSpecificCmdFlags--$"$--commanderPath--$" ebl create "%EBL_FILE%" --app "%TARGET_BPATH%.s37" --device $--partNumber--$ %CONVERT_FLAGS% > "%TARGET_BPATH%-commander-convert-output-ebl.txt""
  @echo off
  type "%TARGET_BPATH%-commander-convert-output-ebl.txt"
  GOTO Done

:GBL
  echo " "
  echo "This converts S37 to Gecko Bootload File format if a bootloader has been selected in AppBuilder"
  echo " "
  @echo on
  cmd /C "$--osSpecificCmdFlags--$"$--commanderPath--$" gbl create "%TARGET_BPATH%.gbl" --app "%TARGET_BPATH%.s37" --device $--partNumber--$ > "%TARGET_BPATH%-commander-convert-output-gbl.txt""
  @echo off
  type "%TARGET_BPATH%-commander-convert-output-gbl.txt"
  GOTO Done

:Done
  @echo on