cd /d "D:\Driver\MouseDriver\MouseDriver" &msbuild "MouseDriver.vcxproj" /t:sdvViewer /p:configuration="Debug" /p:platform="x64" /p:SolutionDir="D:\Driver\MouseDriver" 
exit %errorlevel% 