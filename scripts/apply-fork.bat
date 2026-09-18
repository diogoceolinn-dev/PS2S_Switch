@echo off
REM Aplica o overlay fork\ sobre a arvore de build (copia do xerpi/play-switch).
REM Uso: scripts\apply-fork.bat [dir-da-arvore]  (padrao: upstream\play-switch)
setlocal
cd /d "%~dp0.."
set "DST=%~1"
if "%DST%"=="" set "DST=upstream\play-switch"
if not exist "%DST%\Source" (
  echo [ERRO] arvore de build ausente: %DST%
  echo Clone antes: git clone --recurse-submodules https://github.com/xerpi/play-switch.git
  pause
  exit /b 1
)
xcopy "fork" "%DST%\" /E /I /Y /Q
if errorlevel 1 (
  echo [ERRO] falha ao copiar overlay.
  pause
  exit /b 1
)
echo [OK] overlay Fase 2 aplicado em %DST%
exit /b 0
