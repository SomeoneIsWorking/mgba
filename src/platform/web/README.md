# mGBA Web Multiplayer

A web-based multiplayer frontend for mGBA that allows multiple players to connect via web browsers and play GBA games together using the Link Cable functionality.

## Features

- **WebSocket Server**: Qt-based server that handles multiple client connections
- **Built-in HTTP Server**: Serves the web client files directly (no separate web server needed)
- **Link Cable Emulation**: Uses mGBA's MultiplayerController for authentic multiplayer gameplay
- **Web Client**: HTML5/JavaScript client with virtual gamepad controls
- **Real-time Video**: Streams game video to connected players in real-time
- **Player Display**: Shows player number in the top-left corner
- **Cross-platform**: Works on any device with a modern web browser

## Building

1. Enable the web server in CMake:
   ```bash
   cmake -DBUILD_WEB_SERVER=ON ..
   make
   ```

2. This will create the `mgba-web-server` executable.

## Usage

1. **Start the server**:
   ```bash
   ./mgba-web-server -r kirby.gba -p 8080 -h 8000
   ```

   Options:
   - `-r, --rom <file>`: GBA ROM file to load (default: kirby.gba)
   - `-p, --port <port>`: WebSocket port (default: 8080)
   - `-h, --http-port <port>`: HTTP port for web client (default: 8000)
   - `-w, --web-root <dir>`: Web root directory (default: web)
   - `-m, --max-clients <num>`: Maximum clients 1-4 (default: 4)

2. **Open web browsers**:
   Navigate to `http://localhost:8000` in up to 4 browser windows/tabs

3. **Connect and play**:
   - Each browser window becomes a separate player
   - Use the on-screen controls or keyboard:
     - Arrow keys: D-pad
     - X: A button
     - Z: B button
     - A: L shoulder
     - S: R shoulder
     - Enter/Right Shift: START
     - Space/Left Shift: SELECT

## Controls

### On-screen Controls
- D-pad for movement
- A and B action buttons
- L and R shoulder buttons
- START and SELECT buttons

### Keyboard Controls
- **Arrow Keys**: D-pad movement
- **X**: A button
- **Z**: B button
- **A**: L shoulder button
- **S**: R shoulder button
- **Enter** or **Right Shift**: START
- **Space** or **Left Shift**: SELECT

## Protocol

The client-server communication uses WebSocket with JSON messages:

### Input Messages
```json
{
  "type": "input",
  "action": "press" | "release",
  "key": "a" | "b" | "up" | "down" | "left" | "right" | "l" | "r" | "start" | "select",
  "timestamp": 1234567890
}
```

### Video Messages
```json
{
  "type": "video",
  "data": "base64-encoded-jpeg-frame"
}
```

### Connection Messages
```json
{
  "type": "connected",
  "sessionId": "uuid",
  "playerId": 0
}
```

## Supported Games

Any GBA game that supports Link Cable multiplayer should work, including:
- Kirby & The Amazing Mirror
- The Legend of Zelda: Four Swords Adventures
- Mario Kart: Super Circuit
- Final Fantasy Crystal Chronicles
- And many more!

## Performance Notes

- Video frames are encoded as JPEG at 80% quality for bandwidth efficiency
- Frame rate is capped at ~60 FPS
- WebSocket connections are kept alive with periodic ping messages
- The server can handle up to 4 simultaneous players

## Troubleshooting

### Server won't start
- Check that the specified ports are not in use
- Ensure the ROM file exists and is readable
- Verify Qt WebSockets module is installed

### Client can't connect
- Check firewall settings
- Ensure WebSocket port is accessible
- Try a different browser
- Check browser console for errors

### Poor performance
- Reduce number of simultaneous players
- Check network bandwidth
- Close other browser tabs/applications
- Use a wired connection instead of WiFi