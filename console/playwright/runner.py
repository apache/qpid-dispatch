import asyncio
import re
import subprocess
import sys
from typing import Tuple


async def start_dispatch(build: str) -> Tuple[asyncio.subprocess.Process, int]:
    p = await asyncio.subprocess.create_subprocess_shell(
        f"source {build}/config.sh; {build}/router/qdrouterd -c ./qdrouterd.conf",
        stderr=asyncio.subprocess.PIPE)

    while True:
        line = (await p.stderr.readline()).decode()
        print('router says:', line, end='')
        m = re.search(r'SERVER \(notice\) Listening for HTTP on localhost:(\d\d+) \(console\)', line)
        if m:
            port = int(m.group(1))
            break

    return p, port


async def print_output(stream: asyncio.StreamReader):
    while True:
        line = await stream.readline()
        if line == b'':
            return
        print("router says:", line.decode(), end="")


async def main() -> int:
    dispatch_build = sys.argv[1]
    dispatch, console_port = await start_dispatch(dispatch_build)
    print("router finished starting up, console listening at port", console_port)
    printer = print_output(dispatch.stderr)
    # await asyncio.sleep(100)
    subprocess.check_call("yarn run playwright test", shell=True, env={
        'baseURL': f'http://localhost:{console_port}',
    })
    dispatch.terminate()
    await printer
    return await dispatch.wait()


if __name__ == '__main__':
    sys.exit(asyncio.run(main()))
