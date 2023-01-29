@echo off

# We are supposed to be in the repo's work-tree root; at least
# that seems to be the case when run from a GH Action...

pushd .
call :main
if errorlevel 1 echo - ERROR: SFML setup failed.
popd
goto :eof


:main
setlocal

if "%SFML_LIBROOT%"=="" set "SFML_LIBROOT=tmp/SFML"

if exist "%SFML_LIBROOT%" (
	echo - WARNING: "%SFML_LIBROOT%" already exists^!
	if "%GITHUB_ACTIONS%"=="" (
		echo - Refusing to proceed.
		exit /b 101
	)

) else (
	md "%SFML_LIBROOT%"
)

set "_SFML_PACK_NAME=windows-vc17-64.zip"
set "_SFML_DOWNLOAD_URL=https://artifacts.sfml-dev.org/by-branch/master/%_SFML_PACK_NAME%"
::NOTE: pkg. layout: ./<commit-hash>/[lib|include|...]
set "_SFML_DOWNLOAD_DIR=%SFML_LIBROOT%"
set "_SFML_DOWNLOADED_PACK=%_SFML_DOWNLOAD_DIR%/%_SFML_PACK_NAME%"

rem set _SFML
rem set SFML

::
echo Download...
::
%~dp0busybox wget -P %_SFML_DOWNLOAD_DIR% %_SFML_DOWNLOAD_URL%

if not exist "%_SFML_DOWNLOADED_PACK%" (
	echo - ERROR: Failed to download package.
	exit /b 1
)

::
echo Unpack...
::
if not "%GITHUB_ACTIONS%"=="" set Z7_PROCEED=-y

7z x "%_SFML_DOWNLOADED_PACK%" "-o%SFML_LIBROOT%" %Z7_PROCEED%

if errorlevel 1 (
	echo - ERROR: unzipping failed.
	exit /b 2
)

if not exist "%SFML_LIBROOT%/*" (
	echo - ERROR: Failed to unpack package?^!
	exit /b 3
)

rem This is supposed to clear the ERRORLEVEL...:
rem call

::
echo Fixup lib dir...
::
cd %SFML_LIBROOT%

::These craps fail when run from a batch file! :-o
::pushd *
::cd *
for /d %%i in (*) do (
	cd %%i
	goto break
)
:break

if exist "lib" if exist "include" (
	mv * ..
) else (
	echo - ERROR: Unexpected package layout ^(should have "lib" and "include"^).
	goto error_exit
	exit /b 4
)

endlocal
exit /b 0
