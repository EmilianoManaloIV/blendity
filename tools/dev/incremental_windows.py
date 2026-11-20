# SPDX-License-Identifier: GPL-2.0-or-later
"""Local Windows build worker. build.bat owns compiler, source and deployment selection."""

import argparse
from contextlib import contextmanager
import ctypes
from ctypes import wintypes
import hashlib
import json
import locale
import msvcrt
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import time
import uuid


ROOT = Path(__file__).resolve().parents[2]
SCHEMA = 2
TARGETS = {
    'app': 'Blendity.exe',
    'tests': 'blendity_tests.exe',
}
TOOL_ENV = (
    'PATH', 'INCLUDE', 'LIB', 'LIBPATH', 'CL', '_CL_', 'LINK', '_LINK_',
    'VCToolsVersion', 'VCToolsInstallDir', 'VCToolsRedistDir', 'WindowsSdkDir',
    'WindowsSDKVersion', 'UniversalCRTSdkDir', 'UCRTVersion', 'VSCMD_ARG_TGT_ARCH',
    'VSCMD_ARG_HOST_ARCH', 'BLENDITY_LIBDIR', 'BLENDITY_BUILD', 'OPTIX_ROOT_DIR',
)
COMPILE_ENV = ('CL', '_CL_', 'INCLUDE', 'WindowsSdkDir', 'WindowsSDKVersion',
               'UniversalCRTSdkDir', 'UCRTVersion', 'VSCMD_ARG_TGT_ARCH')
LINK_ENV = ('LINK', '_LINK_', 'LIB', 'LIBPATH', 'WindowsSdkDir', 'WindowsSDKVersion',
            'UniversalCRTSdkDir', 'UCRTVersion')
SEARCHING_LIB = re.compile(r'^\s*Searching\s+(.+\.lib):\s*$', re.I | re.M)
CPP_TOKEN = re.compile(
    rb'(?P<raw>(?:u8|u|U|L)?R"(?P<delimiter>[^()\\\x00-\x20]{0,16})\(.*?\)(?P=delimiter)")'
    rb'|(?P<comment>//[^\r\n]*|/\*.*?\*/)'
    rb'|"(?:\\.|[^"\\\r\n])*"|\'(?:\\.|[^\'\\\r\n])*\'', re.S)
DIRECTIVE = re.compile(rb'(?m)^[ \t\v\f]*(?:\#|%:|\?\?=)[ \t\v\f]*([a-z_]+)[ \t\v\f]*(.*)$')
MACRO_NAME = re.compile(rb'[a-zA-Z_]\w*')
HEADER_NAME = re.compile(rb'<([^>\r\n]+)>|"([^"\r\n]+)"')
HAS_INCLUDE = re.compile(rb'\b__has_include(?:_next)?\b')
DEFINED_OPERATOR = re.compile(rb'\bdefined[ \t]*(?:\([ \t]*[a-zA-Z_]\w*[ \t]*\)|[ \t]+[a-zA-Z_]\w*)')
HAS_INCLUDE_DEFINED = re.compile(rb'\bdefined\s*(?:\(\s*__has_include(?:_next)?\s*\)|\s+__has_include(?:_next)?\b)')
PASTED_PROBE = re.compile(rb'\b(?:__has_?|_?has_include(?:_next)?|_include(?:_next)?)\b')


class BuildError(RuntimeError):
    pass


def say(message):
    print(f'[blendity] {message}', flush=True)


def write_json(path, value):
    with path.open('w', encoding='utf-8') as stream:
        json.dump(value, stream, ensure_ascii=False, sort_keys=True, separators=(',', ':'))
        stream.write('\n')
        stream.flush()
        os.fsync(stream.fileno())


def load_json(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def identity(value):
    data = json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(',', ':'))
    return hashlib.sha256(data.encode('utf-8')).hexdigest()


def path_key(path):
    return os.path.normcase(os.path.abspath(path))


def argv(command):
    # The batch resolver emits Windows command-line quoting (for example /I"C:\Program Files\...").
    shell = ctypes.windll.shell32
    shell.CommandLineToArgvW.argtypes = [wintypes.LPCWSTR, ctypes.POINTER(ctypes.c_int)]
    shell.CommandLineToArgvW.restype = ctypes.POINTER(wintypes.LPWSTR)
    free = ctypes.windll.kernel32.LocalFree
    free.argtypes = [ctypes.c_void_p]
    free.restype = ctypes.c_void_p
    count = ctypes.c_int()
    arguments = shell.CommandLineToArgvW('tool ' + command, ctypes.byref(count))
    if not arguments:
        raise BuildError('Could not parse the resolved Windows command line')
    try:
        return arguments[1:count.value]
    finally:
        free(arguments)


def split_paths(value):
    return [Path(item.strip('"')) for item in value.split(';') if item.strip('"')]


def flag_paths(arguments, option):
    iterator = iter(arguments)
    for argument in iterator:
        if argument.lower().startswith('/' + option):
            value = argument[1 + len(option):] or next(iterator, '')
            if not value:
                raise BuildError(f'Missing path for /{option}')
            yield Path(value)


def read_lines(path):
    # cmd redirection uses the console code page. Run the resolver under UTF-8.
    return path.read_text(encoding='utf-8').splitlines()


def record_resolved_plan():
    plan = Path(os.environ['_BLENDITY_INCREMENTAL_PLAN'])
    environment = {name: os.environ.get(name, '') for name in TOOL_ENV}
    tools = {}
    for name in ('cl', 'link'):
        # The clean batch invokes cl with cmd's search order, which checks cwd before PATH.
        tools[name] = shutil.which(name + '.exe', path=os.getcwd() + ';' + environment['PATH'])
    write_json(plan / 'resolved.json', {
        'compile': argv(os.environ['FLAGS']),
        'link': argv(os.environ['LINKFLAGS']),
        'target_flags': {'app': argv(os.environ['APP_LINK']), 'tests': argv(os.environ['CONSOLE_LINK'])},
        'tools': tools,
        'environment': environment,
    })


@contextmanager
def cache_lock(cache):
    # Windows releases the byte-range lock automatically when its owning process exits.
    # Keep the lock file permanently so waiters always open the same file object.
    with (cache / 'writer.lock').open('a+b', buffering=0) as stream:
        said_waiting = False
        while True:
            stream.seek(0)
            try:
                msvcrt.locking(stream.fileno(), msvcrt.LK_NBLCK, 1)
                break
            except OSError as error:
                if error.errno not in (13, 36):
                    raise
                if not said_waiting:
                    say(f'Waiting for incremental cache writer: {cache}')
                    said_waiting = True
                time.sleep(0.25)
        try:
            yield
        finally:
            stream.seek(0)
            msvcrt.locking(stream.fileno(), msvcrt.LK_UNLCK, 1)


class Files:
    def __init__(self):
        self.contents = {}

    def digest(self, path):
        key = path_key(path)
        if key not in self.contents:
            with Path(path).open('rb') as stream:
                before = os.fstat(stream.fileno())
                digest = hashlib.file_digest(stream, 'sha256').hexdigest()
                after = os.fstat(stream.fileno())
            if self.stamp(before) != self.stamp(after):
                raise BuildError(f'File changed while being read: {path}')
            self.contents[key] = (self.stamp(after), digest)
        return self.contents[key][1]

    @staticmethod
    def stamp(info):
        # Windows stat() and fstat() can expose different ctime semantics for the same open file.
        return info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns

    def confirm(self, path):
        previous, _ = self.contents[path_key(path)]
        if self.stamp(Path(path).stat()) != previous:
            raise BuildError(f'Build input changed during this request: {path}')

    def matches(self, path, expected):
        try:
            return self.digest(path) == expected
        except OSError:
            return False


def inspect_toolchain(resolved, expected, files):
    environment = resolved['environment']
    version = environment['VCToolsVersion'].rstrip('/\\')
    directory = Path(environment['VCToolsInstallDir'])
    if not version or not (version == expected or version.startswith(expected + '.')):
        raise BuildError(f'Requested MSVC {expected}, but the active toolset is {version or "unknown"}')
    if directory.name != version or not all(resolved['tools'].values()):
        raise BuildError(f'Could not identify cl/link in the active MSVC {version} installation')
    compiler, linker = (Path(resolved['tools'][name]) for name in ('cl', 'link'))
    bin_directory = compiler.parent
    expected_bin = directory / 'bin' / bin_directory.parent.name / 'x64'
    if path_key(bin_directory) != path_key(expected_bin) or path_key(linker.parent) != path_key(expected_bin):
        raise BuildError(f'Requested MSVC {expected}, but cl/link on PATH differ: {compiler}, {linker}')
    host_tools = set(bin_directory.glob('c1*.dll')) | set(bin_directory.glob('mspdb*.dll'))
    compiler_inputs = sorted(host_tools | {compiler, bin_directory / 'c2.dll'})
    compiler_identity = {path_key(path): files.digest(path) for path in compiler_inputs}
    linker_inputs = sorted(host_tools | {linker, bin_directory / 'c2.dll', bin_directory / 'cvtres.exe'})
    linker_identity = {path_key(path): files.digest(path) for path in linker_inputs}
    say(f'MSVC {version}: cl={compiler} link={linker}')
    say(f'Tool fingerprints: cl={compiler_identity[path_key(compiler)]} link={linker_identity[path_key(linker)]}')
    return compiler, linker, compiler_identity, linker_identity


def tool_flags(resolved):
    environment = resolved['environment']
    # CL options are inherited by cl.exe. Refuse mode/output overrides that would bypass the
    # worker's per-source output and dependency reporting. Linker options stay with the linker.
    cl_options = argv(environment['CL']) + argv(environment['_CL_'])
    remaining = iter(cl_options)
    for option in remaining:
        if option.startswith('/FI'):
            if option == '/FI' and not next(remaining, None):
                raise BuildError('Missing operand after CL/_CL_ option /FI')
            continue
        if not option.startswith('/') or not option.lower().startswith(
                ('/d', '/u', '/i', '/o', '/w', '/fp:', '/zc:', '/arch:', '/favor:')):
            raise BuildError(f'Unsupported CL/_CL_ option in an incremental build: {option}')
        if option.lower().startswith(('/interface', '/ifc', '/internalpartition', '/doc')):
            raise BuildError(f'Unsupported CL/_CL_ option in an incremental build: {option}')
        if option.lower() in ('/d', '/u', '/i') and not next(remaining, None):
            raise BuildError(f'Missing operand after CL/_CL_ option {option}')
    link_options = argv(environment['LINK']) + argv(environment['_LINK_'])
    for option in link_options:
        lowered = option.lower()
        if (lowered not in ('/incremental:no', '/nologo', '/ltcg', '/debug', '/debug:full', '/verbose')
                and not lowered.startswith(('/include:', '/defaultlib:', '/nodefaultlib', '/opt:', '/verbose:'))):
            raise BuildError(f'Unsupported LINK/_LINK_ option in an incremental build: {option}')
    return cl_options


def enumerate_files(root):
    def raise_walk_error(error):
        raise error

    if not root.exists():
        return
    for directory, _, names in os.walk(root, onerror=raise_walk_error):
        for name in names:
            yield Path(directory) / name


def resolved_include_path(path):
    try:
        return path.resolve()
    except RuntimeError:
        # Python 3.11/3.12 can raise on link loops even with strict=False.
        # The unresolved prefix will be checked by the normal name probes.
        return path


def include_namespace(roots, cache):
    # Snapshot names from the ordinary include tree. Linked subtrees are probed
    # by relevant name so junctions, cycles and dangling links stay bounded.
    names = set()
    aliases = {}

    def register_alias(path, target):
        if cache.is_relative_to(target):
            raise BuildError(f'Incremental cache {cache} is inside include search path {path}. '
                             'Set BLENDITY_INCREMENTAL_DIR to a directory outside the include search roots.')
        if target != path:
            aliases[path_key(path)] = path_key(target)

    for root in roots:
        target = resolved_include_path(root)
        register_alias(root, target)
        if target != root or not root.is_dir():
            continue
        pending = [root]
        while pending:
            with os.scandir(pending.pop()) as entries:
                for entry in entries:
                    path = Path(entry.path)
                    info = entry.stat(follow_symlinks=False)
                    if info.st_file_attributes & stat.FILE_ATTRIBUTE_REPARSE_POINT:
                        register_alias(path, resolved_include_path(path))
                        # Track reparse points even if the provider does not redirect the path.
                        aliases.setdefault(path_key(path), path_key(path))
                    elif stat.S_ISDIR(info.st_mode):
                        pending.append(path)
                    else:
                        names.add(path_key(path))
    return names, aliases


def candidate_path(path):
    if not os.path.isfile(path):
        return None
    # A quoted include can cross a link outside the snapshotted roots. Remember
    # where the name resolved as well as whether it existed.
    return path_key(Path(path).resolve(strict=True))


def scan_preprocessor(path):
    raw = Path(path).read_bytes()
    if raw.startswith((b'\xff\xfe', b'\xfe\xff')):
        raw = raw.decode('utf-16').encode('utf-8')
    raw = raw.removeprefix(b'\xef\xbb\xbf').replace(b'\r\n', b'\n').replace(b'\r', b'\n')
    raw = raw.replace(b'\\\n', b'').replace(b'??/\n', b'')

    def comment_to_space(match):
        token = match[0]
        return b' ' + b'\n' * token.count(b'\n') if match['comment'] or match['raw'] else token

    macros = {}
    operands = set()
    macro_bodies = {}
    condition_names = set()
    opaque = False
    for directive, body in DIRECTIVE.findall(CPP_TOKEN.sub(comment_to_space, raw)):
        if directive == b'define':
            name = MACRO_NAME.match(body)
            if name:
                value = body[name.end():]
                macros.setdefault(name[0], set()).add(None if value.startswith(b'(') else value.strip())
                macro_bodies.setdefault(name[0], []).append(value)
        if directive in (b'if', b'elif'):
            condition = DEFINED_OPERATOR.sub(b'', body)
            opaque |= does_token_paste(condition)
            condition_names.update(MACRO_NAME.findall(condition))
        if directive in (b'include', b'include_next'):
            operands.add(body.strip())
        elif directive in (b'import', b'using'):
            opaque = True
        elif directive in (b'if', b'elif', b'define'):
            # A macro wrapper or pasted spelling of the probe operator requires the real
            # preprocessor. Direct probes can be modeled from their complete operand.
            body = HAS_INCLUDE_DEFINED.sub(b'', body)
            probes = list(HAS_INCLUDE.finditer(body))
            opaque |= bool(PASTED_PROBE.search(HAS_INCLUDE.sub(b'', body)))
            for probe in probes:
                match = re.match(rb'\s*\(\s*(<[^>\r\n]+>|"[^"\r\n]+"|[a-zA-Z_]\w*)\s*\)',
                                 body[probe.end():])
                if match and directive != b'define':
                    operands.add(match[1])
                else:
                    opaque = True
    return macros, operands, opaque, macro_bodies, condition_names


def does_token_paste(text):
    return any(operator in text for operator in (b'##', b'%:%:', b'??=??='))


def command_macros(arguments):
    macros = {}
    remaining = iter(arguments)
    for argument in remaining:
        if not argument.startswith(('/D', '/d')):
            continue
        definition = argument[2:] or next(remaining, '')
        # cl also accepts '#' in CL/_CL_ as a substitute for '='.
        name, separator, value = os.fsencode(definition).replace(b'#', b'=', 1).partition(b'=')
        if MACRO_NAME.fullmatch(name):
            macros.setdefault(name, set()).add(value if separator else b'1')
    return macros


class IncludeSearch:
    def __init__(self, include_dirs, namespace_roots, arguments, aliases=None):
        self.include_dirs = {path_key(path) for path in include_dirs}
        self.prefixes = tuple(str(root).rstrip('\\') + '\\' for root in namespace_roots)
        self.aliases = aliases or {}
        self.alias_prefixes = tuple(path.rstrip('\\') + '\\' for path in self.aliases)
        self.command_macros = command_macros(arguments)
        self.forced = {os.fsencode(path) for path in flag_paths(arguments, 'fi')}
        self.scanned = {}
        self.probes = {}

    @staticmethod
    def headers(operand, definitions, visited=frozenset()):
        header = HEADER_NAME.fullmatch(operand) if operand is not None else None
        if header:
            angle, quoted = header.groups()
            # In a macro replacement, tokens inside <...> may be expanded again.
            if angle and visited:
                if re.search(rb'\s', angle) or any(definitions(name) or name.startswith(b'_')
                                                  for name in MACRO_NAME.findall(angle)):
                    return None
            return {(angle or quoted, bool(quoted))}
        if operand is None or not MACRO_NAME.fullmatch(operand) or operand in visited:
            return None
        replacements = definitions(operand)
        if not replacements:
            # The pinned MSVC's predefined macros use reserved names. An unbound
            # project name cannot supply a header in a successful active directive.
            return None if operand.startswith(b'_') else set()
        result = set()
        for replacement in replacements:
            expanded = IncludeSearch.headers(replacement, definitions, visited | {operand})
            if expanded is None:
                return None
            result.update(expanded)
        return result

    def outside_candidates(self, dependencies):
        for path in dependencies:
            if path not in self.scanned:
                self.scanned[path] = scan_preprocessor(path)
        parsed = [self.scanned[path] for path in dependencies]
        if any(item[2] for item in parsed) or self.condition_pastes_tokens(parsed):
            return None
        operands = set().union(*(item[1] for item in parsed))
        definitions_by_name = {}

        def definitions(name):
            if name not in definitions_by_name:
                values = self.command_macros.get(name, set()).copy()
                for item in parsed:
                    values.update(item[0].get(name, ()))
                definitions_by_name[name] = values
            return definitions_by_name[name]

        headers = {(name, True) for name in self.forced}
        for operand in operands:
            expanded = self.headers(operand, definitions)
            if expanded is None:
                return None
            headers.update(expanded)
        quote_dirs = self.include_dirs | {os.path.dirname(path) for path in dependencies}
        if self.forced:
            quote_dirs.add(path_key(ROOT))
        present = {}
        for raw_name, quoted in headers:
            name = os.fsdecode(raw_name)
            may_escape = '..' in name or ':' in name or name.startswith(('/', '\\'))
            for directory in quote_dirs if quoted else self.include_dirs:
                # The membership snapshot already covers normal names under these roots.
                if not self.aliases and not may_escape and (directory + '\\').startswith(self.prefixes):
                    continue
                candidate = path_key(os.path.join(directory, name))
                if (candidate.startswith(self.prefixes) and candidate not in self.aliases
                        and not candidate.startswith(self.alias_prefixes)):
                    continue
                if candidate not in self.probes:
                    self.probes[candidate] = candidate_path(candidate)
                if self.probes[candidate]:
                    present[candidate] = self.probes[candidate]
        return [path if path == target else (path, target) for path, target in sorted(present.items())]

    def condition_pastes_tokens(self, parsed):
        # A macro can manufacture the complete __has_include token without spelling
        # it anywhere in the project. Ask MSVC whenever conditional expansion can paste.
        pending = set().union(*(item[4] for item in parsed))
        visited = set()
        while pending:
            name = pending.pop()
            if name in visited:
                continue
            visited.add(name)
            bodies = self.command_macros.get(name, ())
            for body in bodies:
                if does_token_paste(body) or HAS_INCLUDE.search(body):
                    return True
                pending.update(MACRO_NAME.findall(body))
            for item in parsed:
                for body in item[3].get(name, ()):
                    if does_token_paste(body):
                        return True
                    pending.update(MACRO_NAME.findall(body))
        return False

    def unchanged(self):
        return all(candidate_path(path) == target for path, target in self.probes.items())


def source_inputs(record, source, files, search):
    names = record.get('includes')
    if not isinstance(names, list) or not all(isinstance(path, str) for path in names):
        return None, False
    dependencies = [path_key(source), *names]
    try:
        contents = [(path, files.digest(path)) for path in dependencies]
        candidates = search.outside_candidates(dependencies)
        return identity([contents, candidates]), candidates is None
    except (OSError, UnicodeError):
        return None, False


def compiler_dependencies(path, source):
    try:
        report = load_json(path)
        data = report['Data']
        includes = data['Includes']
        if (report['Version'] not in ('1.0', '1.1', '1.2')
                or path_key(data['Source']) != path_key(source) or not isinstance(includes, list)
                or not all(isinstance(name, str) and Path(name).is_absolute() for name in includes)
                or data.get('ImportedModules') or data.get('ImportedHeaderUnits')
                or data.get('ProvidedModule') or data.get('PCH')):
            return None
        return sorted(set(map(path_key, includes)))
    except (OSError, ValueError, TypeError, KeyError):
        return None


def response_file(path, arguments, link_tail=None):
    # cl passes only the remainder of the /link line to link.exe. The object and
    # source lists can each span multiple lines, including non-ASCII worktree paths.
    lines = [subprocess.list2cmdline([item]) for item in arguments]
    if link_tail:
        lines.append(subprocess.list2cmdline(link_tail))
    path.write_text('\n'.join(lines) + '\n', encoding='utf-16')


def run_compiles(compiler, flags, sources, work, environment):
    object_dir = work / 'objects'
    dependency_dir = work / 'dependencies'
    object_dir.mkdir()
    dependency_dir.mkdir()
    response = work / 'compile.rsp'
    response_file(response, ['/c', *flags, '/Fo' + str(object_dir) + '\\', *sources])
    subprocess.run([compiler, '/sourceDependencies', str(dependency_dir), '@' + str(response)],
                   cwd=ROOT, env=environment, check=True)
    return object_dir, dependency_dir



def preprocess(compiler, flags, sources, work, environment):
    if not sources:
        return {}
    started = time.perf_counter()
    with tempfile.TemporaryDirectory(prefix='preprocess-', dir=work) as folder:
        directory = Path(folder)
        response = directory / 'preprocess.rsp'
        response_file(response, [*flags, '/P', '/Fi' + str(directory) + '\\', *sources])
        subprocess.run([compiler, '@' + str(response)], cwd=ROOT, env=environment, check=True)
        results = {}
        for source in sources:
            with (directory / (Path(source).stem + '.i')).open('rb') as stream:
                results[source] = hashlib.file_digest(stream, 'sha256').hexdigest()
    say(f'Preprocessed {len(sources)} TUs for include validation in {time.perf_counter() - started:.3f} seconds')
    return results


def lib_search_dirs(environment, link_flags):
    return [ROOT, *flag_paths(link_flags, 'libpath:'), *split_paths(environment['LIB']),
            *split_paths(environment['LIBPATH'])]


def libraries_identity(names, directories, files, require_resolved=False):
    # Include both resolved contents and the search candidates. If a library appears in an
    # earlier directory on LIB, the previous link no longer describes what link.exe will use.
    paths = set()
    for name in names:
        path = Path(name)
        if path.is_absolute():
            if path.is_file():
                paths.add(path_key(path))
            elif require_resolved:
                raise BuildError(f'Linker library is no longer readable: {path}')
        if not path.is_absolute() and (ROOT / path).is_file():
            paths.add(path_key(ROOT / path))
        for directory in directories:
            candidate = directory / (path.name if path.is_absolute() else path)
            if candidate.is_file():
                paths.add(path_key(candidate))
    return identity([(path, files.digest(path)) for path in sorted(paths)])


def run_link(compiler, flags, link_flags, objects, output, work, environment):
    response = work / (output.stem + '.rsp')
    response_file(response, [*flags, '/Fe:' + str(output), *map(str, objects)],
                  [*link_flags, '/VERBOSE:LIB'])
    log = work / (output.stem + '.link.log')
    with log.open('wb') as stream:
        result = subprocess.run([compiler, '@' + str(response)], cwd=ROOT, env=environment,
                                stdout=stream, stderr=subprocess.STDOUT)
    text = log.read_text(encoding=locale.getpreferredencoding(False), errors='replace')
    diagnostic = [line for line in text.splitlines() if re.search(r': (warning|error|fatal error) [A-Z]+\d+', line)]
    if result.returncode:
        say(f'Link failed for {output.name}')
        print('\n'.join(diagnostic) or text, flush=True)
        result.check_returncode()
    if diagnostic:
        print('\n'.join(diagnostic), flush=True)
    libraries = set(SEARCHING_LIB.findall(text))
    if not libraries:
        raise BuildError(f'Could not establish resolved libraries for {output.name}')
    return sorted(libraries)


def add_tree(layout, source, target):
    for path in enumerate_files(source):
        layout[(target / path.relative_to(source)).as_posix()] = path


def runtime_files(work):
    layout = {}
    add_tree(layout, ROOT / 'licenses', Path('licenses'))
    dlls = {}
    for entry in read_lines(work / 'deploy.txt'):
        kind, raw = entry.split('|', 1)
        path = Path(raw.strip('"'))
        if kind == 'dll':
            dlls[path.name.lower()] = path
        elif kind == 'ocio':
            add_tree(layout, path, Path('datafiles/colormanagement'))
        else:
            raise BuildError(f'Unknown deployment record: {entry}')
    layout.update({path.name: path for path in dlls.values()})
    if 'embree4.dll' in dlls and not any(name.startswith('sycl') for name in dlls):
        say('WARNING: embree4.dll needs the dpcpp SYCL runtime, which is absent from the package.')
    return layout


def inventory(directory, files):
    return {path.relative_to(directory).as_posix(): files.digest(path)
            for path in enumerate_files(directory)}


def matches_inventory(directory, expected, files):
    try:
        return directory.is_dir() and inventory(directory, files) == expected
    except OSError:
        return False


def copy_items(entries, into):
    for name, source in entries.items():
        destination = into / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)


def stage_package(package, links, targets, layout, files, previous, cache, environment):
    runtime = {name: files.digest(path) for name, path in layout.items()}
    staged = dict(layout)
    for target in targets:
        for name in links[target]['outputs']:
            staged[name] = cache / links[target]['path'] / name
    should_warm = 'app' in targets and not (environment.get('BLENDITY_NO_LIBS') or
                                             environment.get('BLENDITY_NO_WARM'))
    key = identity([runtime, {target: links[target]['outputs'] for target in targets}, should_warm])
    if previous and previous['key'] == key and previous.get('warmed'):
        existing = cache / previous['path']
        if matches_inventory(existing, previous['files'], files):
            return previous
    package.mkdir()
    copy_items(staged, package)
    for name, source in staged.items():
        if files.digest(package / name) != files.digest(source):
            raise BuildError(f'Staged file differs from its source: {name}')
    warmed = True
    if should_warm:
        # Run alongside the exact runtime shipped with the package. A missing GPU is the
        # clean build's supported fallback, and can be retried when a package is repaired.
        warm_environment = dict(environment, BLENDITY_CACHE_DIR=(environment.get('BLENDITY_CACHE_DIR')
                                                               or str(cache / 'shader-cache')))
        result = subprocess.run([package / TARGETS['app'], '--no-libs', '--warm-shaders', package / 'shaders'],
                                cwd=ROOT, env=warm_environment)
        if result.returncode == 1:
            warmed = False
            say('No GPU shaders compiled ahead. The first GPU view compiles them.')
        elif result.returncode:
            result.check_returncode()
    return {'key': key, 'files': inventory(package, files), 'warmed': warmed}


def load_state(cache):
    fresh = {'schema': SCHEMA, 'worktree': path_key(ROOT), 'configurations': {}}
    try:
        state = load_json(cache / 'current.json')
    except FileNotFoundError:
        return fresh
    if not isinstance(state, dict):
        raise BuildError('Invalid incremental cache manifest')
    if state.get('worktree') != path_key(ROOT):
        raise BuildError(f'Cache belongs to another worktree: {state.get("worktree")}')
    if state.get('schema') != SCHEMA:
        return fresh
    return state


def read_sources(work, targets):
    shared = [line.strip('"') for line in read_lines(work / 'sources.rsp')]
    entries = {name: read_lines(work / (name + '.rsp')) for name in TARGETS}
    selected = {target: shared + entries[target] for target in targets}
    sources = list(dict.fromkeys(source for items in selected.values() for source in items))
    names = [Path(source).stem.lower() for source in sources]
    if len(names) != len(set(names)):
        raise BuildError('The selected sources have duplicate object names, which the clean build also cannot link')
    return selected, sources, {path_key(source) for source in [*shared, *entries['app'], *entries['tests']]}


def resolve(work, expected, target):
    environment = dict(os.environ, BLENDITY_VCVARS_VER=expected, BLENDITY_VCVARS_STRICT='1',
                       _BLENDITY_INCREMENTAL_PLAN=str(work))
    subprocess.run([os.environ.get('COMSPEC', 'cmd.exe'), '/d', '/c',
                    'chcp 65001 >nul && build.bat release ' + target],
                   cwd=ROOT, env=environment, check=True)
    return load_json(work / 'resolved.json')


def compiled_inputs(sources, dependency_dir, files, search, compile_started):
    for source in sources:
        report = dependency_dir / (Path(source).name + '.json')
        includes = compiler_dependencies(report, source)
        for path in includes or ():
            files.digest(path)
            if files.contents[path_key(path)][0][3] > compile_started:
                raise BuildError(f'Build input changed after compilation started: {path}')
        inputs, needs_preprocessing = source_inputs({'includes': includes}, source, files, search)
        yield source, (includes, inputs, needs_preprocessing)


def refresh_objects(records, sources, namespace_changed, files, cache, work, compiler,
                    flags, environment, search):
    previous = {source: records.get(path_key(source), {}) for source in sources}
    input_checks = {source: source_inputs(record, source, files, search)
                    for source, record in previous.items()}
    before = preprocess(compiler, flags, [src for src, (_, needs) in input_checks.items() if needs],
                        work, environment)
    wanted = []
    for source, (inputs, _) in input_checks.items():
        record = previous[source]
        if (namespace_changed or inputs is None or inputs != record.get('inputs')
                or before.get(source) != record.get('preprocessed')
                or not files.matches(cache / record['path'], record['digest'])):
            wanted.append(source)
    say(f'Compiling {len(wanted)} of {len(sources)} objects')
    if not wanted:
        return
    for source in wanted:
        files.digest(source)
    compile_started = time.time_ns()
    object_dir, dependency_dir = run_compiles(compiler, flags, wanted, work, environment)
    results = dict(compiled_inputs(wanted, dependency_dir, files, search, compile_started))
    opaque = [source for source, (_, _, needs) in results.items() if needs]
    discovered = [source for source in opaque if source not in before]
    if discovered:
        # The dependency report can reveal a new opaque directive after the initial
        # compile. Bracket a fresh normal compile with matching native preprocessing
        # so its object and preprocessor fingerprint describe the same inputs.
        before.update(preprocess(compiler, flags, discovered, work, environment))
        say(f'Recompiling {len(discovered)} objects with newly discovered opaque includes')
        with tempfile.TemporaryDirectory(prefix='recompile-', dir=work) as folder:
            repeated_objects, repeated_reports = run_compiles(compiler, flags, discovered, Path(folder), environment)
            results.update(compiled_inputs(discovered, repeated_reports, files, search, compile_started))
            for path in repeated_objects.iterdir():
                os.replace(path, object_dir / path.name)
    after = preprocess(compiler, flags, opaque, work, environment)
    if any(after[source] != before[source] for source in opaque):
        raise BuildError('Preprocessed inputs changed during compilation')
    for source, (includes, inputs, _) in results.items():
        object_path = object_dir / (Path(source).stem + '.obj')
        if inputs is None:
            say(f'Could not verify include inputs for {source}; this object will be rebuilt on the next request')
        records[path_key(source)] = {'includes': includes, 'inputs': inputs,
                                     'preprocessed': after.get(source),
                                     'path': object_path.relative_to(cache).as_posix(),
                                     'digest': files.digest(object_path)}


def refresh_links(links, selected, records, files, cache, work, compiler, flags,
                  link_flags, target_flags, environment, link_config):
    directories = lib_search_dirs(environment, link_flags)
    explicit_libraries = [flag for flag in link_flags if flag.lower().endswith('.lib')]
    relinked = []
    for target, sources in selected.items():
        output, options = TARGETS[target], target_flags[target]
        objects = [records[path_key(source)] for source in sources]
        obj_key = identity([link_config, options, [(path_key(source), obj['inputs'], obj.get('preprocessed'), obj['digest'])
                                                   for source, obj in zip(sources, objects)]])
        previous = links.get(target, {})
        if (obj_key == previous.get('objects') and
                libraries_identity(previous['libraries'], directories, files) == previous['library_key'] and
                matches_inventory(cache / previous['path'], previous['outputs'], files)):
            continue
        say(f'Linking {output}')
        location = work / target
        location.mkdir()
        libraries = run_link(compiler, flags, [*link_flags, *options],
                             [cache / obj['path'] for obj in objects], location / output, work, environment)
        libraries = sorted(set(libraries) | set(explicit_libraries))
        links[target] = {'objects': obj_key, 'sources': sources, 'libraries': libraries,
                         'library_key': libraries_identity(libraries, directories, files, require_resolved=True),
                         'path': location.relative_to(cache).as_posix(), 'outputs': inventory(location, files)}
        relinked.append(target)
    say(f'Linked {len(relinked)} targets' + (': ' + ', '.join(relinked) if relinked else ''))


def build(cache, work, expected, target):
    resolved = resolve(work, expected, target)
    files = Files()
    compiler, _, compiler_identity, linker_identity = inspect_toolchain(resolved, expected, files)
    state = load_state(cache)
    targets = tuple(TARGETS) if target == 'app-tests' else (target,)
    selected, sources, translation_units = read_sources(work, targets)
    flags, link_flags = resolved['compile'], resolved['link']
    tool_environment = resolved['environment']
    supplied = {key.upper() for key in tool_environment}
    environment = {key: value for key, value in os.environ.items() if key.upper() not in supplied}
    environment.update(tool_environment, VSLANG='1033')
    cl_options = tool_flags(resolved)
    include_dirs = list(flag_paths([*cl_options, *flags], 'i')) + split_paths(environment['INCLUDE'])
    # Compiler include roots expose whole directory trees. Source and quoted-header
    # directories expose only their actual candidates, which IncludeSearch probes.
    namespace_roots = tuple(dict.fromkeys(Path(path_key(root)) for root in include_dirs))
    namespace, aliases = include_namespace(namespace_roots, cache)
    names = sorted(namespace)
    namespace_key = identity([names, sorted(aliases.items())] if aliases else names)
    compile_config = identity([flags, compiler_identity, {name: environment[name] for name in COMPILE_ENV}])
    link_config = identity([flags, link_flags, compiler_identity, linker_identity,
                            {name: environment[name] for name in LINK_ENV}])
    say(f'Compile flags: {subprocess.list2cmdline(flags)}')
    say(f'Link flags: {subprocess.list2cmdline(link_flags)}')
    for name, options in resolved['target_flags'].items():
        if name in targets:
            say(f'{name} link flags: {subprocess.list2cmdline(options)}')
    for name in sorted(set(COMPILE_ENV + LINK_ENV + ('BLENDITY_LIBDIR', 'OPTIX_ROOT_DIR'))):
        say(f'{name}={environment[name]}')
    config = state['configurations'].setdefault(compile_config, {'objects': {}, 'links': {}, 'packages': {}})
    changed = namespace_key != config.get('includes')
    records = config['objects']
    if changed:
        # Drop unrequested objects when their include namespace changes. The previous
        # selected reports can still identify which sources need preprocessing.
        selected_paths = {path_key(source) for source in sources}
        records = {path: record for path, record in records.items() if path in selected_paths}
    config['includes'] = namespace_key
    search = IncludeSearch(include_dirs, namespace_roots, [*cl_options, *flags], aliases)
    refresh_objects(records, sources, changed, files, cache, work, compiler, flags, environment, search)
    config['objects'] = {path: record for path, record in records.items() if path in translation_units}
    refresh_links(config['links'], selected, records, files, cache, work, compiler, flags, link_flags,
                  resolved['target_flags'], environment, link_config)
    package = work / 'package'
    staged = stage_package(package, config['links'], targets, runtime_files(work), files,
                           config['packages'].get(target), cache, environment)
    if package.exists():
        staged['path'] = package.relative_to(cache).as_posix()
    config['packages'][target] = staged
    if include_namespace(namespace_roots, cache) != (namespace, aliases) or not search.unchanged():
        raise BuildError('Include search candidates changed during the request')
    for path in files.contents:
        files.confirm(path)
    state['package'] = staged['path']
    pending = work / 'state.json'
    write_json(pending, state)
    os.replace(pending, cache / 'current.json')
    return state


def remove_owned_directory(path, cache):
    if not path.resolve().is_relative_to(cache / 'generations') or path.resolve() == cache / 'generations':
        raise BuildError(f'Refusing to remove a directory outside the cache generations: {path}')
    shutil.rmtree(path)


def prune_artifacts(cache, generation, state):
    # Packages can still be running after their successor is published. Keep them in place.
    # The linker and object artifacts are private to the worker and can be pruned under its lock.
    configurations = state['configurations'].values()
    objects = {cache / record['path'] for cfg in configurations for record in cfg['objects'].values()}
    links = {cache / record['path'] for cfg in configurations for record in cfg['links'].values()}
    for directory in (cache / 'generations').iterdir():
        if not re.fullmatch(r'[0-9a-f]{32}', directory.name) or not directory.is_dir():
            continue
        # A prior process could have died with a child still writing its request. Avoid
        # touching generations that were never committed and still contain their plan.
        if directory != generation and (directory / 'resolved.json').exists():
            continue
        obj_dir = directory / 'objects'
        if obj_dir.is_dir():
            for path in obj_dir.glob('*.obj'):
                if path not in objects:
                    path.unlink()
            if not any(obj_dir.iterdir()):
                obj_dir.rmdir()
        for target in TARGETS:
            output = directory / target
            if output not in links and output.is_dir():
                remove_owned_directory(output, cache)
        if not any(directory.iterdir()):
            directory.rmdir()
    if generation.exists():
        for path in generation.iterdir():
            if path.is_file():
                path.unlink()
        dependencies = generation / 'dependencies'
        if dependencies.exists():
            remove_owned_directory(dependencies, cache)
        if not any(generation.iterdir()):
            generation.rmdir()


def main():
    if sys.version_info < (3, 11):
        raise BuildError('The local incremental build needs Python 3.11 or newer')
    # Relative MSVC inputs and cache paths are interpreted from the worktree.
    os.chdir(ROOT)
    if sys.argv[1:] == ['--resolve']:
        record_resolved_plan()
        return 0
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('configuration', choices=['release'], type=str.lower)
    parser.add_argument('target', choices=['app', 'tests', 'app-tests'], type=str.lower)
    args = parser.parse_args()
    if os.environ.get('BLENDITY_OUT'):
        raise BuildError('Use the reported package instead of BLENDITY_OUT for an incremental build')
    expected = os.environ.get('BLENDITY_VCVARS_VER') or '14.51'
    if not re.fullmatch(r'\d+\.\d+(?:\.\d+)?', expected):
        raise BuildError('BLENDITY_VCVARS_VER must identify an installed MSVC toolset (for example 14.51)')
    cache = Path(os.environ.get('BLENDITY_INCREMENTAL_DIR') or ROOT / 'build' / 'incremental').resolve()
    cache.mkdir(parents=True, exist_ok=True)
    generations = cache / 'generations'
    if generations.resolve() != generations:
        raise BuildError(f'Cache generations must be a directory in the cache: {generations}')
    with cache_lock(cache):
        started = time.perf_counter()
        generation = generations / uuid.uuid4().hex
        generation.mkdir(parents=True)
        try:
            state = build(cache, generation, expected, args.target)
        except (OSError, ValueError, BuildError, subprocess.CalledProcessError):
            # The child has exited. An abrupt interruption leaves the unique generation
            # alone in case its child is still writing, and the manifest still points back.
            try:
                remove_owned_directory(generation, cache)
            except OSError as error:
                say(f'Could not remove failed request {generation}: {error}')
            raise
        try:
            prune_artifacts(cache, generation, state)
        except (OSError, BuildError) as error:
            # The published package and state are already committed.
            say(f'Could not prune unused cache artifacts: {error}')
        say(f'Package: {cache / state["package"]}')
        say(f'Incremental request completed in {time.perf_counter() - started:.3f} seconds')
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, ValueError, BuildError, subprocess.CalledProcessError) as error:
        say(f'Incremental build failed: {error}')
        sys.exit(1)
