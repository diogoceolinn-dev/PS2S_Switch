@echo off
REM Prepara toolchain + clona o upstream Play! (rodar no cmd do Windows).
setlocal
chcp 65001 >nul

where git >nul 2>nul
if errorlevel 1 (
  echo [ERRO] git nao encontrado. Instale o Git e rode de novo.
  pause
  exit /b 1
)

if not exist "C:\devkitPro" (
  echo [ERRO] devkitPro nao encontrado em C:\devkitPro. Instale em https://devkitpro.org
  pause
  exit /b 1
)
echo [OK] devkitPro encontrado.

echo.
echo [1/2] Instale no shell msys do devkitPro:
echo   dkp-pacman -S switch-dev switch-mesa switch-glfw switch-sdl2
echo   (se o dkp-pacman ainda nao estiver configurado ai, configure os
echo    repositorios devkitPro antes de continuar)
echo.
pause

echo [2/2] Clonando Play! com submodulos...
if not exist "Play-" (
  git clone --recurse-submodules https://github.com/jpd002/Play-.git
  if errorlevel 1 (
    echo [ERRO] clone falhou. Verifique a internet.
    pause
    exit /b 1
  )
) else (
  echo [OK] pasta Play- ja existe, pulando clone.
)

echo.
echo [OK] Pronto. Proximo passo: docs\BUILD.md (cmake com Switch.cmake).
pause
exit /b 0
