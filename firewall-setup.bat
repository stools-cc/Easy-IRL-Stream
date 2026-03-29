@echo off
echo Creating firewall rules for Easy IRL Stream...

netsh advfirewall firewall add rule name="Easy IRL Stream - SRT (UDP)" dir=in action=allow protocol=UDP localport=9000 profile=private,public
netsh advfirewall firewall add rule name="Easy IRL Stream - RTMP (TCP)" dir=in action=allow protocol=TCP localport=1935 profile=private,public
netsh advfirewall firewall add rule name="Easy IRL Stream - SRTLA (UDP)" dir=in action=allow protocol=UDP localport=5000 profile=private,public

echo.
echo Done! SRT (UDP 9000), RTMP (TCP 1935) and SRTLA (UDP 5000) are now allowed.
pause
