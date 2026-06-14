import socket

class HydroDBError(Exception):
    pass

class HydroDB:
    def __init__(self, host='127.0.0.1', port=7379):
        self.host = host
        self.port = port
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.connect()

    def connect(self):
        try:
            self.sock.connect((self.host, self.port))

        except Exception as e:
            raise HydroDBError(f"Could not connect to HydroDB at {self.host}:{self.port} - {e}")

    def close(self):
        self.sock.close()

    def _send_command(self, cmd):
        self.sock.sendall((cmd + '\n').encode('utf-8'))

    def _read_line(self):
        line = b""
        while True:
            char = self.sock.recv(1)
            if not char:
                raise HydroDBError("Connection closed by server")
            line += char
            if line.endswith(b'\n'):
                # Handle both \n and \r\n
                return line.strip(b'\r\n').decode('utf-8')

    def _parse_response(self):
        line = self._read_line()
        
        if not line:
            return None

        prefix = line[0]
        
        # Simple string (e.g. +OK)
        if prefix == '+':
            return line[1:]
            
        # Error (e.g. -ERR ...)
        elif prefix == '-':
            raise HydroDBError(line[1:])
            
        # Integer (e.g. :1)
        elif prefix == ':':
            return int(line[1:])
            
        # Bulk string (e.g. $5\r\nhello\r\n or $-1\r\n)
        elif prefix == '$':
            length = int(line[1:])
            if length == -1:
                return None
            
            # Read the exact number of bytes + 2 for \r\n
            data = b""
            while len(data) < length + 2:
                chunk = self.sock.recv(min(4096, length + 2 - len(data)))
                data += chunk
            
            return data[:-2].decode('utf-8')
            
        # Array (e.g. *2\r\n$3\r\nkey\r\n$3\r\nval\r\n)
        elif prefix == '*':
            count = int(line[1:])
            result = []
            for _ in range(count):
                result.append(self._parse_response())
            return result
            
        else:
            raise HydroDBError(f"Unknown protocol response: {line}")

    # --- Public API Methods ---

    def set(self, key, value):
        """Set key to hold the string value."""
        self._send_command(f"SET {key} {value}")
        res = self._parse_response()
        return res == 'OK'

    def get(self, key):
        """Get the value of key."""
        self._send_command(f"GET {key}")
        return self._parse_response()

    def delete(self, key):
        """Remove the specified key."""
        self._send_command(f"DEL {key}")
        return bool(self._parse_response())

    def range(self, start_key, end_key, limit=None, offset=0):
        """Return all key-value pairs between start_key and end_key."""
        cmd = f"ZRANGE {start_key} {end_key}"
        if limit is not None:
            cmd += f" LIMIT {offset} {limit}"
            
        self._send_command(cmd)
        arr = self._parse_response()
        if arr is None: return {}
        
        # Convert flat array [k1, v1, k2, v2] to Dictionary
        res_dict = {}
        for i in range(0, len(arr), 2):
            res_dict[arr[i]] = arr[i+1]
        return res_dict
