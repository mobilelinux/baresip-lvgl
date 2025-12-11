#!/bin/bash
# SIP Registration Diagnostic Script

echo "=== SIP Server Connectivity Test ==="
echo ""

# Check if server is reachable
SERVER="172.16.1.97"
PORT="7060"

echo "1. Testing network connectivity to $SERVER..."
ping -c 3 $SERVER

echo ""
echo "2. Testing SIP port $PORT..."
nc -zv -w 3 $SERVER $PORT 2>&1

echo ""
echo "3. Current Baresip logs (if any):"
if [ -f ~/.baresip/log.txt ]; then
    tail -50 ~/.baresip/log.txt
else
    echo "No Baresip log file found"
fi

echo ""
echo "4. Checking local network interfaces:"
ifconfig | grep -A 1 "inet "

echo ""
echo "=== Next Steps to Debug ==="
echo "- If ping fails: Check network connection"
echo "- If port is closed: Verify SIP server is running on $SERVER:$PORT"
echo "- Check if SIP server requires authentication"
echo "- Verify account credentials (username: 808084, password: 12345)"
