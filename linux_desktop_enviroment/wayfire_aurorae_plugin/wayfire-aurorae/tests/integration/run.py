#!/usr/bin/env python3
"""End-to-end test of the plugin in a real (headless) Wayfire.

usage: tests/integration/run.py [--theme DIR] [--gpu] [--keep]

Starts Wayfire with the headless backend and the plugin from ./build, opens
foot windows and drives a virtual pointer through Wayfire's test IPC
(stipc): button clicks, title bar double-click and drag, border and
extended-area resizing, fullscreen, live reconfiguration. Button positions
come from `aurorae-preview --dump`, so any theme can be used.

Needs: wayfire (with ipc, ipc-rules, stipc plugins), foot. Screenshots
(--keep) need grim; set GRIM=/path/to/grim if it is not on PATH.
"""
import argparse, json, os, re, shutil, socket, struct, subprocess, sys, tempfile, time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BUILD = os.path.join(ROOT, 'build')
CLIENT = (480, 260)


class Wayfire:
    def __init__(self, theme, gpu, workdir):
        self.dir = workdir
        os.makedirs(os.path.join(workdir, 'config/foot'), exist_ok=True)
        with open(os.path.join(workdir, 'config/foot/foot.ini'), 'w') as f:
            f.write('[csd]\npreferred=server\n')
        ini = os.path.join(workdir, 'wayfire.ini')
        with open(ini, 'w') as f:
            f.write(f'''[core]
plugins = ipc ipc-rules stipc aurorae move resize wm-actions
preferred_decoration_mode = server
xwayland = false
vwidth = 1
vheight = 1
[output:HEADLESS-1]
mode = 1000x700@60000
[aurorae]
theme = {theme}
font = Noto Sans 10
[wm-actions]
toggle_fullscreen = <super> KEY_F
''')
        self.sock = os.path.join(os.environ.get('XDG_RUNTIME_DIR', '/tmp'), f'aurorae-it-{os.getpid()}.sock')
        env = dict(os.environ)
        for var in ('WAYLAND_DISPLAY', 'DISPLAY'):
            env.pop(var, None)
        env.update({
            'WLR_BACKENDS': 'headless', 'WLR_HEADLESS_OUTPUTS': '1', 'WLR_LIBINPUT_NO_DEVICES': '1',
            'WAYFIRE_PLUGIN_PATH': BUILD, 'WAYFIRE_PLUGIN_XML_PATH': os.path.join(ROOT, 'metadata'),
            '_WAYFIRE_SOCKET': self.sock, 'XDG_CONFIG_HOME': os.path.join(workdir, 'config'),
        })
        if gpu:
            env['WLR_RENDER_DRM_DEVICE'] = '/dev/dri/renderD128'
        self.env = env
        self.log = open(os.path.join(workdir, 'wayfire.log'), 'w')
        self.proc = subprocess.Popen(['wayfire', '-c', ini], env=env, stdout=self.log, stderr=subprocess.STDOUT)
        for _ in range(100):
            if os.path.exists(self.sock):
                break
            time.sleep(0.1)
        time.sleep(0.5)
        self.s = socket.socket(socket.AF_UNIX)
        self.s.connect(self.sock)
        self.display = self.call('stipc/get_display')['wayland']

    def call(self, method, data=None):
        msg = json.dumps({'method': method, 'data': data or {}}).encode()
        self.s.sendall(struct.pack('<I', len(msg)) + msg)
        size = struct.unpack('<I', self._recv(4))[0]
        return json.loads(self._recv(size))

    def _recv(self, n):
        data = b''
        while len(data) < n:
            chunk = self.s.recv(n - len(data))
            if not chunk:
                raise EOFError('wayfire closed the IPC connection (crashed?)')
            data += chunk
        return data

    def views(self):
        return [v for v in self.call('window-rules/list-views') if v.get('type') == 'toplevel']

    def view(self, vid):
        return next((v for v in self.views() if v['id'] == vid), None)

    def open_window(self, title):
        before = {v['id'] for v in self.views()}
        self.call('stipc/run', {'cmd': f"foot --title '{title}' --window-size-pixels={CLIENT[0]}x{CLIENT[1]}"})
        for _ in range(100):
            new = [v for v in self.views() if v['id'] not in before]
            if new:
                time.sleep(0.8)
                return new[0]['id']
            time.sleep(0.1)
        raise RuntimeError('window did not appear')

    def place(self, vid, x, y, w, h):
        self.call('stipc/layout_views', {'views': [{'id': vid, 'x': x, 'y': y, 'width': w, 'height': h}]})
        time.sleep(0.4)

    def move(self, x, y):
        self.call('stipc/move_cursor', {'x': x, 'y': y})

    def button(self, mode='full', name='BTN_LEFT'):
        self.call('stipc/feed_button', {'combo': name, 'mode': mode})

    def keys(self, *seq):
        for key, pressed in seq:
            self.call('stipc/feed_key', {'key': key, 'state': pressed})

    def drag(self, start, delta, steps=10):
        self.move(*start)
        self.button('press')
        time.sleep(0.05)
        for i in range(1, steps + 1):
            self.move(start[0] + delta[0] * i / steps, start[1] + delta[1] * i / steps)
            time.sleep(0.02)
        self.button('release')
        time.sleep(0.5)

    def screenshot(self, path):
        env = dict(self.env, WAYLAND_DISPLAY=self.display)
        subprocess.run([os.environ.get('GRIM', 'grim'), path], env=env, check=True)

    def close(self):
        self.proc.terminate()
        try:
            self.proc.wait(5)
        except subprocess.TimeoutExpired:
            self.proc.kill()
        self.log.close()


def layout(theme, client=CLIENT, maximized=False):
    """Decoration geometry from aurorae-preview --dump."""
    flags = ['--maximized'] if maximized else []
    out = subprocess.run([os.path.join(BUILD, 'aurorae-preview'), '--theme', theme, '--hide', 'S',
                          '--size', f'{int(client[0])}x{int(client[1])}', '--dump', *flags],
                         capture_output=True, text=True, check=True).stdout
    l, t, r, b = map(float, re.search(r'borders\s+l=(\S+) t=(\S+) r=(\S+) b=(\S+)', out).groups())
    buttons = {}
    for kind, x, y, w, h in re.findall(r'button\s+(\d+) at (\S+),(\S+) (\S+)x(\S+)', out):
        buttons[int(kind)] = (float(x) + float(w) / 2, float(y) + float(h) / 2)
    return {'borders': (l, t, r, b), 'buttons': buttons}


MINIMIZE, MAXIMIZE, CLOSE = 3, 4, 5


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--theme', default=os.path.join(ROOT, 'tests/data/TestTheme'))
    parser.add_argument('--gpu', action='store_true', help='use the GLES renderer instead of pixman')
    parser.add_argument('--keep', action='store_true', help='keep the work dir and take screenshots')
    args = parser.parse_args()
    theme = os.path.abspath(args.theme)

    normal = layout(theme)
    l, t, r, b = normal['borders']
    mt = layout(theme, maximized=True)['borders'][1]

    def layout_for(geometry):
        """Button positions for a window's current wm geometry."""
        is_max = (geometry['width'], geometry['height']) == (1000, 700)
        top, sides, bottom = (mt, 0, 0) if is_max else (t, l + r, b)
        return layout(theme, (geometry['width'] - sides, geometry['height'] - top - bottom), is_max)
    width, height = int(CLIENT[0] + l + r), int(CLIENT[1] + t + b)

    results = []

    def check(name, ok, detail=''):
        results.append(ok)
        print(f"{'PASS' if ok else 'FAIL'}  {name}  {detail}")

    workdir = tempfile.mkdtemp(prefix='aurorae-it-')
    wf = Wayfire(theme, args.gpu, workdir)
    try:
        vid = wf.open_window('Integration')
        wf.place(vid, 100, 80, width, height)
        g = wf.view(vid)['geometry']
        check('decorated size = client + theme borders', (g['width'], g['height']) == (width, height), g)
        if args.keep:
            wf.screenshot(os.path.join(workdir, 'window.png'))

        def at(geometry, kind):
            x, y = layout_for(geometry)['buttons'][kind]
            return geometry['x'] + x, geometry['y'] + y

        if MAXIMIZE in normal['buttons']:
            wf.move(*at(g, MAXIMIZE)); wf.button(); time.sleep(0.8)
            m = wf.view(vid)['geometry']
            check('maximize button', (m['width'], m['height']) == (1000, 700), m)
            wf.move(*at(m, MAXIMIZE)); wf.button(); time.sleep(0.8)
            g = wf.view(vid)['geometry']
            check('restore button', (g['width'], g['height']) == (width, height), g)

        title = (g['x'] + width / 2, g['y'] + t / 2)
        if not any(abs(title[0] - (g['x'] + bx)) < 30 and abs(title[1] - (g['y'] + by)) < 15
                   for bx, by in normal['buttons'].values()):
            wf.move(*title); wf.button(); time.sleep(0.05); wf.button(); time.sleep(0.8)
            m = wf.view(vid)['geometry']
            check('title double-click maximizes', m['width'] == 1000, m)
            wf.move(500, mt / 2); wf.button(); time.sleep(0.05); wf.button(); time.sleep(0.8)
            g = wf.view(vid)['geometry']
            check('title double-click restores', g['width'] == width, g)

            wf.drag((g['x'] + width / 2, g['y'] + t / 2), (60, 40))
            n = wf.view(vid)['geometry']
            check('title drag moves', abs(n['x'] - g['x'] - 60) <= 2 and abs(n['y'] - g['y'] - 40) <= 2, n)
            g = n

        wf.drag((g['x'] + g['width'] - max(1, r / 2), g['y'] + g['height'] / 2), (50, 0))
        n = wf.view(vid)['geometry']
        check('right border resize', n['width'] >= g['width'] + 40, n)
        g = n

        wf.drag((g['x'] + g['width'] / 2, g['y'] - 4), (0, -30))
        n = wf.view(vid)['geometry']
        check('resize above the title bar', n['height'] >= g['height'] + 20, n)
        g = n

        wf.keys(('KEY_LEFTMETA', True), ('KEY_F', True), ('KEY_F', False), ('KEY_LEFTMETA', False))
        time.sleep(0.8)
        f = wf.view(vid)
        check('fullscreen drops the decoration', f['geometry'] == {'x': 0, 'y': 0, 'width': 1000, 'height': 700}, f['geometry'])
        wf.keys(('KEY_LEFTMETA', True), ('KEY_F', True), ('KEY_F', False), ('KEY_LEFTMETA', False))
        time.sleep(0.8)
        n = wf.view(vid)['geometry']
        check('leaving fullscreen restores it', n == g, n)

        wf.call('wayfire/set-config-options', {'aurorae/border_size': 'huge'})
        time.sleep(0.8)
        n = wf.view(vid)['geometry']
        check('live border_size change', n['width'] > g['width'], n)
        wf.call('wayfire/set-config-options', {'aurorae/border_size': 'normal'})
        time.sleep(0.8)
        g = wf.view(vid)['geometry']

        if MINIMIZE in normal['buttons']:
            wf.move(*at(g, MINIMIZE)); wf.button(); time.sleep(0.6)
            check('minimize button', wf.view(vid).get('minimized') is True)

        other = wf.open_window('Second')
        wf.place(other, 300, 200, width, height)
        g = wf.view(other)['geometry']
        wf.move(*at(g, CLOSE)); wf.button(); time.sleep(0.8)
        check('close button closes', wf.view(other) is None)
        check('compositor still running', wf.proc.poll() is None)
    except Exception as e:
        check('no exception', False, repr(e))
    finally:
        wf.close()
        if args.keep:
            print('work dir:', workdir)
        else:
            shutil.rmtree(workdir, ignore_errors=True)

    print(f'{sum(results)}/{len(results)} passed')
    return 0 if all(results) else 1


if __name__ == '__main__':
    sys.exit(main())
