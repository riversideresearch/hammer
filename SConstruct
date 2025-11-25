# -*- python -*-

from __future__ import absolute_import, division, print_function

import os
import os.path
import platform
import subprocess
import sys

default_install_dir='/usr/local'

vars = Variables(None, ARGUMENTS)
vars.Add(PathVariable('DESTDIR', "Root directory to install in (useful for packaging scripts)", None, PathVariable.PathIsDirCreate))
vars.Add(PathVariable('prefix', "Where to install in the FHS", "/usr/local", PathVariable.PathAccept))
vars.Add('python', 'Python interpreter', 'python')

tools = ['default', 'scanreplace']

# add the clang tool if necessary
if os.getenv('CC') == 'clang':
	tools.append('clang')
else:
	# try to detect if cc happens to be clang by inspecting --version
	cc = os.getenv('CC') or 'cc'
	ver = subprocess.run([cc, '--version'], capture_output=True).stdout
	if b'clang' in ver.split():
		tools.append('clang')
		os.environ['CC'] = cc	# make sure we call it as we saw it

envvars = {'PATH' : os.environ['PATH']}
if 'PKG_CONFIG_PATH' in os.environ:
    envvars['PKG_CONFIG_PATH'] = os.environ['PKG_CONFIG_PATH']

env = Environment(ENV = envvars,
                  variables = vars,
                  tools=tools,
                  toolpath=['tools'])

def calcInstallPath(*elements):
    path = os.path.abspath(os.path.join(*map(env.subst, elements)))
    if 'DESTDIR' in env:
        path = os.path.join(env['DESTDIR'], os.path.relpath(path, start='/'))
    return path

rel_prefix = not os.path.isabs(env['prefix'])
env['prefix'] = os.path.abspath(env['prefix'])
if 'DESTDIR' in env:
    env['DESTDIR'] = os.path.abspath(env['DESTDIR'])
    if rel_prefix:
        print('--!!-- You used a relative prefix with a DESTDIR. This is probably not what you', file=sys.stderr)
        print('--!!-- you want; files will be installed in', file=sys.stderr)
        print('--!!--    %s' % (calcInstallPath('$prefix'),), file=sys.stderr)


env['libpath'] = calcInstallPath('$prefix', 'lib')
env['incpath'] = calcInstallPath('$prefix', 'include', 'hammer')
env['parsersincpath'] = calcInstallPath('$prefix', 'include', 'hammer', 'parsers')
env['backendsincpath'] = calcInstallPath('$prefix', 'include', 'hammer', 'backends')
env['pkgconfigpath'] = calcInstallPath('$prefix', 'lib', 'pkgconfig')

with open('VERSION', 'r') as f:
    version = f.read().strip()

# Export version for use in sub-SConscripts
env['VERSION'] = version

env.ScanReplace('libhammer.pc.in', VERSION=version)

AddOption('--variant',
          dest='variant',
          nargs=1, type='choice',
          choices=['debug', 'opt'],
          default='opt',
          action='store',
          help='Build variant (debug or opt)')

AddOption('--coverage',
          dest='coverage',
          default=False,
          action='store_true',
          help='Build with coverage instrumentation')

AddOption('--force-debug',
          dest='force_debug',
          default=False,
          action='store_true',
          help='Build with debug symbols, even in the opt variant')

AddOption('--gprof',
          dest='gprof',
          default=False,
          action="store_true",
          help='Build with profiling instrumentation for gprof')

AddOption('--in-place',
          dest='in_place',
          default=False,
          action='store_true',
          help='Build in-place, rather than in the build/<variant> tree')

AddOption('--no-tests',
          dest='with_tests',
          default=True,
          action='store_false',
          help='Do not build tests')

AddOption('--fPIC',
          dest='fpic',
          default=False,
          action='store_true',
          help='compile with the fPIC flag for use with shared objects')

env['CC'] = os.getenv('CC') or env['CC']
env['CXX'] = os.getenv('CXX') or env['CXX']
env['CFLAGS'] = os.getenv('CFLAGS') or env['CFLAGS']

# Language standard and warnings
# Using -D_POSIX_C_SOURCE=200809L here, not on an ad-hoc basis when,
# #including, is important
env.MergeFlags('-std=c99 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -Wno-unused-parameter -Wno-attributes -Wno-unused-variable')

# Linker options
env.MergeFlags('-lrt')

if GetOption('coverage'):
    env.Append(CCFLAGS=['--coverage'],
               LDFLAGS=['--coverage'],
               LINKFLAGS=['--coverage'])
    if env['CC'] == 'gcc':
        env.Append(LIBS=['gcov'])
    else:
        env.ParseConfig('llvm-config --ldflags')

if GetOption('force_debug'):
    env.Append(CCFLAGS=['-g'])

if GetOption('gprof'):
    if env['CC'] == 'gcc' and env['CXX'] == 'g++':
        env.Append(CCFLAGS=['-pg'],
		   LDFLAGS=['-pg'],
                   LINKFLAGS=['-pg'])
        env['GPROF'] = 1
    else:
        print("Can only use gprof with gcc")
        Exit(1)

if GetOption('fpic'):
    if env['CC'] == 'gcc':
        env.Append(CCFLAGS=['-fPIC'])
    else:
        print("Can only use fPIC with gcc")
        Exit(1) 

dbg = env.Clone(VARIANT='debug')
dbg.Append(CCFLAGS=['-g'])

opt = env.Clone(VARIANT='opt')
opt.Append(CCFLAGS=['-O3'])

if GetOption('variant') == 'debug':
    env = dbg
else:
    env = opt

env['ENV'].update(x for x in os.environ.items() if x[0].startswith('CCC_'))

#rootpath = env['ROOTPATH'] = os.path.abspath('.')
#env.Append(CPPPATH=os.path.join('#', 'hammer'))

testruns = []

targets = ['$libpath',
           '$incpath',
           '$parsersincpath',
           '$backendsincpath',
           '$pkgconfigpath']

Export('env')
Export('testruns')
Export('targets')

if not GetOption('in_place'):
    env['BUILD_BASE'] = 'build/$VARIANT'
    lib = env.SConscript(['src/SConscript'], variant_dir='$BUILD_BASE/src')
    env.Alias('examples', env.SConscript(['examples/SConscript'], variant_dir='$BUILD_BASE/examples'))
else:
    env['BUILD_BASE'] = '.'
    lib = env.SConscript(['src/SConscript'])
    env.Alias(env.SConscript(['examples/SConscript']))

for testrun in testruns:
    env.Alias('test', testrun)

# Add gcov target to generate coverage files in build directory
if GetOption('coverage'):
    build_base = env.subst('$BUILD_BASE')
    # Create a command that runs gcov from the build directory
    # This ensures .gcov files are generated in the build directory
    gcov_script = '''
import os
import subprocess
build_base = "%s"
if os.path.exists(build_base):
    for root, dirs, files in os.walk(build_base):
        for file in files:
            if file.endswith('.gcda'):
                gcov_file = os.path.join(root, file)
                gcov_dir = os.path.dirname(gcov_file)
                # Run gcov from the directory containing the .gcda file
                # This ensures .gcov files are created in the same directory
                # Use -o to specify output directory and pass just the filename
                subprocess.run(['gcov', '-o', gcov_dir, os.path.basename(gcov_file)], 
                             cwd=gcov_dir, 
                             stdout=subprocess.DEVNULL, 
                             stderr=subprocess.DEVNULL)
''' % build_base
    env.Command('gcov', [], 'python -c %s' % repr(gcov_script))

env.Alias('install', targets)
