#!/usr/bin/env python3
# ***************************************************************************
# * Authors:     Carlos Oscar S. Sorzano (coss@cnb.csic.es)
# *              David Maluenda (dmaluenda@cnb.csic.es)
# *              David Strelak (dstrelak@cnb.csic.es)
# *
# *
# * This program is free software; you can redistribute it and/or modify
# * it under the terms of the GNU General Public License as published by
# * the Free Software Foundation; either version 2 of the License, or
# * (at your option) any later version.
# *
# * This program is distributed in the hope that it will be useful,
# * but WITHOUT ANY WARRANTY; without even the implied warranty of
# * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# * GNU General Public License for more details.
# *
# * You should have received a copy of the GNU General Public License
# * along with this program; if not, write to the Free Software
# * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
# * 02111-1307  USA
# *
# *  All comments concerning this program package may be sent to the
# *  e-mail address 'scipion@cnb.csic.es'
# ***************************************************************************/


import os
import sys
from .utils import *
from.environment import Environment


class Config:
    FILE_NAME = 'xmipp.conf'
    KEY_BUILD_TESTS = 'BUILD_TESTS'

    def __init__(self, askUser=False):
        self.ask = askUser
        self._create_empty()

    def create(self):
        print("Configuring -----------------------------------------")
        self._create_empty()

        if self.configDict['VERIFIED'] == '':
            self.configDict['VERIFIED'] = 'False'

        self._config_compiler()
        self._config_CUDA()
        self._config_MPI()
        self._config_Java()

        # configMatlab(new_config_dict)
        # configStarPU(new_config_dict)
        # config_DL(new_config_dict)
        # configConfigVersion(new_config_dict)
        # configTests(new_config_dict)

        self.write()
        self.environment.write()
        print(blue("Configuration completed....."))

    def check(self):
        print("Checking configuration ------------------------------")
        if self.configDict['VERIFIED'] != 'True':
            if not self._check_compiler:
                print(red("Cannot compile"))
                print("Possible solutions")  # FIXME: check libraries
                print("In Ubuntu: sudo apt-get -y install libsqlite3-dev libfftw3-dev libhdf5-dev libopencv-dev python3-dev "
                      "python3-numpy python3-scipy python3-mpi4py")
                print(
                    "In Manjaro: sudo pacman -Syu install hdf5 python3-numpy python3-scipy --noconfirm")
                print("Please, see 'https://scipion-em.github.io/docs/docs/scipion-modes/"
                      "install-from-sources.html#step-2-dependencies' for more information about libraries dependencies.")
                print("\nRemember to re-run './xmipp config' after installing libraries in order to "
                      "take into account the new system configuration.")
                runJob("rm xmipp_test_main*")
                return False
            if not self._check_MPI():
                print(red("Cannot compile with MPI or use it"))
                runJob("rm xmipp_mpi_test_main*")
                return False
            if not self._check_Java():
                print(red("Cannot compile with Java"))
                runJob("rm Xmipp.java Xmipp.class xmipp_jni_test*")
                return False
            if not self._check_CUDA():
                print(red("Cannot compile with NVCC, continuing without CUDA"))
                # if fails, the test files remains
                runJob("rm xmipp_cuda_test*")
                self.configDict["CUDA"] = "False"
            # if not checkMatlab(configDict):
            #     print(red("Cannot compile with Matlab, continuing without Matlab"))
            #     self.configDict["MATLAB"]="False"
            #     runJob("rm xmipp_mex*")
            # if not checkStarPU(configDict):
            #     print(red("Cannot compile with StarPU, continuing without StarPU"))
            #     self.configDict["STARPU"]="False"
            self.configDict['VERIFIED'] = "True"
        else:
            print(blue("'%s' is already checked. Set VERIFIED=False to re-checked"
                       % Config.FILE_NAME))
        return True

    def get(self):
        return self.configDict

    def writeEnviron(self):  # FIXME remove
        self.environment.write()

    def updateXmippEnv(self, pos='begin', realPath=True, **kwargs):  # FIXME remove
        self.environment.update(pos, realPath, **kwargs)

    def is_true(self, key):
        return self.configDict and (key in self.configDict) and (self.configDict[key].lower() == 'true')

    def read(self, fnConfig=FILE_NAME):
        try:
            from ConfigParser import ConfigParser, ParsingError
        except ImportError:
            from configparser import ConfigParser, ParsingError  # Python 3
        cf = ConfigParser()
        cf.optionxform = str  # keep case (stackoverflow.com/questions/1611799)
        try:
            if os.path.isdir(fnConfig):
                if os.path.exists(os.path.join(fnConfig, Config.FILE_NAME)):
                    fnConfig = os.path.join(fnConfig, Config.FILE_NAME)
                else:
                    fnConfig = os.path.join(fnConfig, "xmipp.template")
            if os.path.exists(fnConfig):
                cf.read(fnConfig)
                if not 'BUILD' in cf.sections():
                    print(red("Cannot find section BUILD in %s" % fnConfig))
                    self._create_empty()
                self.configDict = dict(cf.items('BUILD'))
        except:
            sys.exit("%s\nPlease fix the configuration file %s." %
                     (sys.exc_info()[1], fnConfig))

    def write(self):
        with open(Config.FILE_NAME, "w") as configFile:
            configFile.write("[BUILD]\n")
            for label in sorted(self.configDict.keys()):
                configFile.write("%s=%s\n" % (label, self.configDict[label]))

    def _create_empty(self):
        labels = [Config.KEY_BUILD_TESTS, 'CC', 'CXX', 'LINKERFORPROGRAMS', 'INCDIRFLAGS', 'LIBDIRFLAGS', 'CCFLAGS', 'CXXFLAGS',
                  'LINKFLAGS', 'PYTHONINCFLAGS', 'MPI_CC', 'MPI_CXX', 'MPI_RUN', 'MPI_LINKERFORPROGRAMS', 'MPI_CXXFLAGS',
                  'MPI_LINKFLAGS', 'NVCC', 'CXX_CUDA', 'NVCC_CXXFLAGS', 'NVCC_LINKFLAGS',
                  'MATLAB_DIR', 'CUDA', 'DEBUG', 'MATLAB', 'OPENCV', 'OPENCVSUPPORTSCUDA', 'OPENCV3',
                  'JAVA_HOME', 'JAVA_BINDIR', 'JAVAC', 'JAR', 'JNI_CPPPATH',
                  'STARPU', 'STARPU_HOME', 'STARPU_INCLUDE', 'STARPU_LIB', 'STARPU_LIBRARY',
                  'USE_DL', 'VERIFIED', 'CONFIG_VERSION', 'PYTHON_LIB']
        self.configDict = {}
        self.environment = Environment()
        for label in labels:
            # We let to set up the xmipp configuration via environ.
            self.configDict[label] = os.environ.get(label, "")

    def _config_OpenCV(self):
        cppProg = "#include <opencv2/core/core.hpp>\n"
        cppProg += "int main(){}\n"
        with open("xmipp_test_opencv.cpp", "w") as cppFile:
            cppFile.write(cppProg)

        if not runJob("%s -c -w %s xmipp_test_opencv.cpp -o xmipp_test_opencv.o %s"
                      % (self.configDict["CXX"], self.configDict["CXXFLAGS"],
                         self.configDict["INCDIRFLAGS"]), show_output=False):
            print(yellow("OpenCV not found"))
            self.configDict["OPENCV"] = False
            self.configDict["OPENCVSUPPORTSCUDA"] = False
            self.configDict["OPENCV3"] = False
        else:
            self.configDict["OPENCV"] = True

            # Check version
            with open("xmipp_test_opencv.cpp", "w") as cppFile:
                cppFile.write('#include <opencv2/core/version.hpp>\n')
                cppFile.write('#include <fstream>\n')
                cppFile.write('int main()'
                              '{std::ofstream fh;'
                              ' fh.open("xmipp_test_opencv.txt");'
                              ' fh << CV_MAJOR_VERSION << std::endl;'
                              ' fh.close();'
                              '}\n')
            if not runJob("%s -w %s xmipp_test_opencv.cpp -o xmipp_test_opencv %s "
                          % (self.configDict["CXX"], self.configDict["CXXFLAGS"],
                             self.configDict["INCDIRFLAGS"]), show_output=False):
                self.configDict["OPENCV3"] = False
                version = 2  # Just in case
            else:
                runJob("./xmipp_test_opencv")
                f = open("xmipp_test_opencv.txt")
                versionStr = f.readline()
                f.close()
                version = int(versionStr.split('.', 1)[0])
                self.configDict["OPENCV3"] = version >= 3

            # Check CUDA Support
            cppProg = "#include <opencv2/core/version.hpp>\n"
            cppProg += "#include <opencv2/cudaoptflow.hpp>\n" if self.configDict[
                "OPENCV3"] else "#include <opencv2/core/cuda.hpp>\n"
            cppProg += "int main(){}\n"
            with open("xmipp_test_opencv.cpp", "w") as cppFile:
                cppFile.write(cppProg)
            self.configDict["OPENCVSUPPORTSCUDA"] = runJob("%s -c -w %s xmipp_test_opencv.cpp -o xmipp_test_opencv.o %s" %
                                                           (self.configDict["CXX"], self.configDict["CXXFLAGS"], self.configDict["INCDIRFLAGS"]), show_output=False)

            print(green("OPENCV-%s detected %s CUDA support"
                        % (version, 'with' if self.configDict["OPENCVSUPPORTSCUDA"] else 'without')))
        runJob("rm -v xmipp_test_opencv*", show_output=False)

    def _config_compiler(self):
        if self.configDict["DEBUG"] == "":
            self.configDict["DEBUG"] = "False"

        if self.configDict["CC"] == "" and checkProgram("gcc"):
            self.configDict["CC"] = "gcc"
            print(green('gcc detected'))
        if self.configDict["CXX"] == "":
            if isCIBuild():
                # we can use cache to speed up the build
                self.configDict["CXX"] = "ccache g++" if checkProgram(
                    "g++") else ""
            else:
                self.configDict["CXX"] = "g++" if checkProgram("g++") else ""
        if self.configDict["LINKERFORPROGRAMS"] == "":
            if isCIBuild():
                # we can use cache to speed up the build
                self.configDict["LINKERFORPROGRAMS"] = "ccache g++" if checkProgram(
                    "g++") else ""
            else:
                self.configDict["LINKERFORPROGRAMS"] = "g++" if checkProgram(
                    "g++") else ""

        if self.configDict["CC"] == "gcc":
            if not "-std=c99" in self.configDict["CCFLAGS"]:
                self.configDict["CCFLAGS"] += " -std=c99"
        if 'g++' in self.configDict["CXX"]:
            # optimize for current machine
            self.configDict["CXXFLAGS"] += " -mtune=native -march=native"
            if "-std=c99" not in self.configDict["CXXFLAGS"]:
                self.configDict["CXXFLAGS"] += " -std=c++11"
            if isCIBuild():
                # don't tolerate any warnings on build machine
                self.configDict["CXXFLAGS"] += " -Werror"
                # don't optimize, as it slows down the build
                self.configDict["CXXFLAGS"] += " -O0"
            else:
                self.configDict["CXXFLAGS"] += " -O3"
            if self.is_true("DEBUG"):
                self.configDict["CXXFLAGS"] += " -g"
        # Nothing special to add to LINKFLAGS
        from sysconfig import get_paths
        info = get_paths()

        if self.configDict["LIBDIRFLAGS"] == "":
            # /usr/local/lib or /path/to/virtEnv/lib
            localLib = "%s/lib" % info['data']
            self.configDict["LIBDIRFLAGS"] = "-L%s" % localLib
            self.environment.update(LD_LIBRARY_PATH=localLib)

            # extra libs
            hdf5InLocalLib = findFileInDirList("libhdf5*", localLib)
            isHdf5CppLinking = checkLib(self.configDict['CXX'], '-lhdf5_cpp')
            isHdf5Linking = checkLib(self.configDict['CXX'], '-lhdf5')
            if not (hdf5InLocalLib or (isHdf5CppLinking and isHdf5Linking)):
                print(yellow("\n'libhdf5' not found at '%s'." % localLib))
                hdf5Lib = findFileInDirList("libhdf5*", ["/usr/lib",
                                                         "/usr/lib/x86_64-linux-gnu"])
                hdf5Lib = askPath(hdf5Lib, self.ask)
                if hdf5Lib:
                    self.configDict["LIBDIRFLAGS"] += " -L%s" % hdf5Lib
                    self.environment.update(LD_LIBRARY_PATH=hdf5Lib)
                else:
                    installDepConda('hdf5', self.ask)

        if not checkLib(self.configDict['CXX'], '-lfftw3'):
            print(red("'libfftw3' not found in the system"))
            installDepConda('fftw', self.ask)
        if not checkLib(self.configDict['CXX'], '-ltiff'):
            print(red("'libtiff' not found in the system"))
            installDepConda('libtiff', self.ask)

        if self.configDict["INCDIRFLAGS"] == "":
            # /usr/local/include or /path/to/virtEnv/include
            localInc = "%s/include" % info['data']
            self.configDict["INCDIRFLAGS"] += ' '.join(
                map(lambda x: '-I' + str(x), getDependenciesInclude()))
            self.configDict["INCDIRFLAGS"] += " -I%s" % localInc

            # extra includes
            if not findFileInDirList("hdf5.h", [localInc, "/usr/include"]):
                print(yellow("\nHeaders for 'libhdf5' not found at '%s'." % localInc))
                # Add more candidates if needed
                hdf5Inc = findFileInDirList(
                    "hdf5.h", "/usr/include/hdf5/serial")
                hdf5Inc = askPath(hdf5Inc, self.ask)
                if hdf5Inc:
                    self.configDict["INCDIRFLAGS"] += " -I%s" % hdf5Inc

        if self.configDict["PYTHON_LIB"] == "":
            # malloc flavour is not needed from 3.8
            malloc = "m" if sys.version_info.minor < 8 else ""
            self.configDict["PYTHON_LIB"] = "python%s.%s%s" % (sys.version_info.major,
                                                               sys.version_info.minor,
                                                               malloc)

        if self.configDict["PYTHONINCFLAGS"] == "":
            import numpy
            incDirs = [info['include'], numpy.get_include()]

            self.configDict["PYTHONINCFLAGS"] = ' '.join(
                ["-I%s" % iDir for iDir in incDirs])

        self.configDict["OPENCV"] = os.environ.get("OPENCV", "")
        if self.configDict["OPENCV"] == "" or self.configDict["OPENCVSUPPORTSCUDA"] or self.configDict["OPENCV3"]:
            self._config_OpenCV()

    def _get_GCC_version(self, compiler):
        log = []
        runJob(compiler + " -dumpversion", show_output=False,
               show_command=False, log=log)
        full_version = log[0].strip()
        tokens = full_version.split('.')
        if len(tokens) < 2:
            tokens.append('0')  # for version 5.0, only '5' is returned
        gccVersion = float(str(tokens[0] + '.' + tokens[1]))
        return gccVersion, full_version

    def _ensure_GCC_GPP_version(self, compiler):
        if not checkProgram(compiler, True):
            sys.exit(-7)
        gccVersion, fullVersion = self._get_GCC_version(compiler)

        if gccVersion < 4.8:  # join first two numbers, i.e. major and minor version
            print(red('Detected ' + compiler + " in version " +
                  fullVersion + '. Version 4.8 or higher is required.'))
            sys.exit(-8)
        else:
            print(green(compiler + ' ' + fullVersion + ' detected'))

    def _ensure_compiler_version(self, compiler):
        if 'g++' in compiler or 'gcc' in compiler:
            self._ensure_GCC_GPP_version(compiler)
        else:
            print(red('Version detection for \'' +
                  compiler + '\' is not implemented.'))

    def _get_Hdf5_name(self, libdirflags):
        libdirs = libdirflags.split("-L")
        for dir in libdirs:
            if os.path.exists(os.path.join(dir.strip(), "libhdf5.so")):
                return "hdf5"
            elif os.path.exists(os.path.join(dir.strip(), "libhdf5_serial.so")):
                return "hdf5_serial"
        return "hdf5"

    def _check_compiler(self):
        print("Checking compiler configuration ...")
        # in case user specified some wrapper of the compiler
        # get rid of it: 'ccache g++' -> 'g++'
        currentCxx = self.configDict["CXX"].split()[-1]
        self._ensure_compiler_versionensureCompilerVersion(currentCxx)

        cppProg = """
    #include <fftw3.h>
    #include <hdf5.h>
    #include <tiffio.h>
    #include <jpeglib.h>
    #include <sqlite3.h>
    #include <pthread.h>
    #include <Python.h>
    #include <numpy/ndarraytypes.h>
        """
        if self.configDict["OPENCV"] == "True":
            cppProg += "#include <opencv2/core/core.hpp>\n"
            if self.configDict["OPENCVSUPPORTSCUDA"] == "True":
                if self.configDict["OPENCV3"] == "True":
                    cppProg += "#include <opencv2/cudaoptflow.hpp>\n"
                else:
                    cppProg += "#include <opencv2/core/cuda.hpp>\n"
        cppProg += "\n int main(){}\n"
        with open("xmipp_test_main.cpp", "w") as cppFile:
            cppFile.write(cppProg)

        if not runJob("%s -c -w %s xmipp_test_main.cpp -o xmipp_test_main.o %s %s" %
                      (self.configDict["CXX"], self.configDict["CXXFLAGS"], self.configDict["INCDIRFLAGS"], self.configDict["PYTHONINCFLAGS"])):
            print(
                red("Check the INCDIRFLAGS, CXX, CXXFLAGS and PYTHONINCFLAGS in xmipp.conf"))
            # FIXME: Check the dependencies list
            print(red("If some of the libraries headers fail, try installing fftw3_dev, tiff_dev, jpeg_dev, sqlite_dev, hdf5, pthread"))
            return False
        libhdf5 = self._get_Hdf5_name(self.configDict["LIBDIRFLAGS"])
        if not runJob("%s %s %s xmipp_test_main.o -o xmipp_test_main -lfftw3 -lfftw3_threads -l%s  -lhdf5_cpp -ltiff -ljpeg -lsqlite3 -lpthread" %
                      (self.configDict["LINKERFORPROGRAMS"], self.configDict["LINKFLAGS"], self.configDict["LIBDIRFLAGS"], libhdf5)):
            print(red("Check the LINKERFORPROGRAMS, LINKFLAGS and LIBDIRFLAGS"))
            return False
        runJob("rm xmipp_test_main*")
        return True

    def _get_CUDA_version(self, nvcc):
        log = []
        runJob(nvcc + " --version", show_output=False,
               show_command=False, log=log)
        # find 'Cuda compilation tools' line (last for older versions, one before last otherwise)
        # expected format: 'Cuda compilation tools, release 8.0, V8.0.61'
        full_version_line = next(l for l in log if 'compilation tools' in l)
        full_version = full_version_line.strip().split(', ')[-1].lstrip('V')
        tokens = full_version.split('.')
        if len(tokens) < 2:
            tokens.append('0')  # just in case when only one digit is returned
        nvccVersion = float(str(tokens[0] + '.' + tokens[1]))
        return nvccVersion, full_version

    def _get_compatible_GCC(self, nvcc_version):
        # https://gist.github.com/ax3l/9489132
        v = ['10.2', '10.1', '10',
             '9.3', '9.2', '9.1', '9',
             '8.4', '8.3', '8.2', '8.1', '8',
             '7.5', '7.4', '7.3', '7.2', '7.1', '7',
             '6.5', '6.4', '6.3', '6.2', '6.1', '6',
             '5.5', '5.4', '5.3', '5.2', '5.1', '5',
             '4.9', '4.8']
        if 8.0 <= nvcc_version < 9.0:
            return v[v.index('5.3'):]
        elif 9.0 <= nvcc_version < 9.2:
            return v[v.index('5.5'):]
        elif 9.2 <= nvcc_version < 10.1:
            return v[v.index('7.3'):]
        elif 10.1 <= nvcc_version <= 10.2:
            return v[v.index('8.4'):]
        elif 11.0 <= nvcc_version <= 11.2:
            return v[v.index('9.3'):]
        return []  # not supported

    def _config_CUDA(self):
        self.configDict["CUDA"] = os.environ.get("CUDA", "")
        nvcc = 'nvcc'
        if self.configDict["CUDA"] == "":
            environCudaBin = os.environ.get('XMIPP_CUDA_BIN',
                                            os.environ.get('CUDA_BIN', ''))
            cudaBin = whereis(nvcc, findReal=True,
                              env=environCudaBin + ':' + os.environ.get('PATH', ''))
            if cudaBin:
                self.configDict["CUDA"] = "True"
                nvcc = os.path.join(cudaBin, nvcc)
            else:
                print(yellow("\n'nvcc' not found in the PATH "
                             "(either in CUDA_BIN/XMIPP_CUDA_BIN)"))
                cudaBin = findFileInDirList('nvcc', ["/usr/local/cuda/bin",
                                                     "/usr/local/cuda*/bin"])  # check order
                cudaBin = askPath(cudaBin, self.ask)
                if os.path.isfile(os.path.join(cudaBin, 'nvcc')):
                    self.configDict["CUDA"] = "True"
                    # If using generic cuda, expliciting the version
                    cudaBin = os.path.realpath(cudaBin)
                    nvcc = os.path.join(cudaBin, nvcc)
                else:
                    print(
                        yellow("CUDA not found. Continuing only with CPU integration."))
                    self.configDict["CUDA"] = "False"
        self.environment.update(CUDA=self.configDict["CUDA"] == "True")
        if self.configDict["CUDA"] == "True":
            if self.configDict["NVCC"] == "":
                if checkProgram(nvcc):
                    nvccVersion, nvccFullVersion = self._get_CUDA_version(nvcc)
                    print(green('CUDA-' + nvccFullVersion + ' detected.'))
                    if nvccVersion != 10.2:
                        print(yellow('CUDA-10.2 is recommended.'))
                    self.configDict["NVCC"] = nvcc
                else:
                    print(yellow("Warning: 'nvcc' not found. "
                                 "'NVCC_CXXFLAGS' and 'NVCC_LINKFLAGS' cannot be "
                                 "automatically set. Please, manual set them or "
                                 "set 'CUDA=False' in the config file."))
                    return
            if self.configDict["NVCC_CXXFLAGS"] == "":
                # in case user specified some wrapper of the compiler
                # get rid of it: 'ccache g++' -> 'g++'
                currentCxx = self.configDict["CXX"].split()[-1]
                cxxVersion, cxxStrVersion = self._get_GCC_version(currentCxx)
                nvccVersion, nvccFullVersion = self._get_CUDA_version(
                    self.configDict["NVCC"])
                if self.configDict["CXX_CUDA"] == '':
                    print(
                        yellow('Checking for compatible GCC to be used with your CUDA'))
                    candidates = self._get_compatible_GCC(nvccVersion)
                    for c in candidates:
                        p = 'g++-' + c
                        if checkProgram(p, False):
                            self.configDict["CXX_CUDA"] = p
                            break
                    if self.configDict["CXX_CUDA"]:
                        self.configDict["CXX_CUDA"] = askPath(
                            self.configDict["CXX_CUDA"], self.ask)
                    if not checkProgram(self.configDict["CXX_CUDA"], False):
                        print(red("No valid compiler found. "
                                  "Skipping CUDA compilation.\n"
                                  "To manually set the compiler, export CXX_CUDA=/path/to/requested_compiler' and "
                                  "run again 'xmipp config'."))
                        self.configDict["CUDA"] = "False"
                        self.environment.update(CUDA=False, pos='replace')
                        return
                if nvccVersion >= 11:
                    self.configDict["NVCC_CXXFLAGS"] = ("--x cu -D_FORCE_INLINES -Xcompiler -fPIC "
                                                        "-ccbin %(CXX_CUDA)s -std=c++14 --expt-extended-lambda "
                                                        # generate PTX only, and SASS at the runtime (by setting code=virtual_arch)
                                                        "-gencode=arch=compute_60,code=compute_60 "
                                                        "-gencode=arch=compute_61,code=compute_61 "
                                                        "-gencode=arch=compute_75,code=compute_75 "
                                                        "-gencode=arch=compute_86,code=compute_86")
                else:
                    self.configDict["NVCC_CXXFLAGS"] = ("--x cu -D_FORCE_INLINES -Xcompiler -fPIC "
                                                        "-ccbin %(CXX_CUDA)s -std=c++11 --expt-extended-lambda "
                                                        # generate PTX only, and SASS at the runtime (by setting code=virtual_arch)
                                                        "-gencode=arch=compute_35,code=compute_35 "
                                                        "-gencode=arch=compute_50,code=compute_50 "
                                                        "-gencode=arch=compute_60,code=compute_60 "
                                                        "-gencode=arch=compute_61,code=compute_61")

            if self.configDict["NVCC_LINKFLAGS"] == "":
                # Looking for Cuda libraries:
                libDirs = ['lib', 'lib64', 'targets/x86_64-linux/lib',
                           'lib/x86_64-linux-gnu']  # add more condidates

                def checkCudaLib(x): return os.path.isfile(x+"/libcudart.so")

                def searchCudaLib(root, cudaLib, ask=False):
                    check = False
                    for lib in libDirs:
                        candidate = os.path.join(root, lib)
                        if checkCudaLib(candidate):
                            cudaLib = candidate
                            check = True
                            break
                    if check:
                        cudaLib = os.path.realpath(cudaLib)
                        cudaLib = askPath(cudaLib, ask=ask)
                    return cudaLib

                # Looking for user defined XMIPP_CUDA_LIB and CUDA_LIB
                cudaLib = os.environ.get('XMIPP_CUDA_LIB',
                                         os.environ.get('CUDA_LIB', ''))

                nvccDir = whereis(self.configDict["NVCC"])
                if not checkCudaLib(cudaLib) and nvccDir:
                    # Looking for Cuda libs under active nvcc.
                    cudaLib = searchCudaLib(
                        os.path.dirname(nvccDir), cudaLib, False)

                if not checkCudaLib(cudaLib):
                    # Looking for Cuda libs in user root libs.
                    cudaLib = searchCudaLib('/usr', cudaLib, self.ask)

                if checkCudaLib(cudaLib):
                    self.configDict["NVCC_LINKFLAGS"] = ("-L%s" % cudaLib +
                                                         " -L%s/stubs" % cudaLib)  # nvidia-ml is in stubs folder
                    self.environment.update(LD_LIBRARY_PATH=cudaLib)
                    self.environment.update(LD_LIBRARY_PATH=cudaLib+"/stubs")
                else:
                    print(yellow("WARNING: system libraries for CUDA not found!\n"
                                 "         If cuda code is not compiling, "
                                 "please, find 'libcudart.so' and manually add\n"
                                 "         the containing folder (e.g. '/my/cuda/lib') at %s\n"
                                 " > NVCC_LINKFLAGS = -L/my/cuda/lib -L/my/cuda/lib/stubs\n"
                                 "         If the problem persist, set 'CUDA=False' before "
                                 "compiling to skip cuda compilation."
                                 % (Config.FILE_NAME)))

    def _check_CUDA(self):
        if self.configDict["CUDA"] == "True":
            if not checkProgram(self.configDict["NVCC"]):
                return False
            print("Checking CUDA configuration ...")
            cppProg = """
        #include <cuda_runtime.h>
        #include <cufft.h>
        int main(){}
        """
            with open("xmipp_cuda_test.cpp", "w") as cppFile:
                cppFile.write(cppProg)

            if not runJob("%s -c -w %s %s xmipp_cuda_test.cpp -o xmipp_cuda_test.o" %
                          (self.configDict["NVCC"], self.configDict["NVCC_CXXFLAGS"], self.configDict["INCDIRFLAGS"])):
                print(red("Check the NVCC, NVCC_CXXFLAGS and INCDIRFLAGS"))
                return False
            if not runJob("%s %s xmipp_cuda_test.o -o xmipp_cuda_test -lcudart -lcufft" %
                          (self.configDict["NVCC"], self.configDict["NVCC_LINKFLAGS"])):
                print(red("Check the NVCC and NVCC_LINKFLAGS"))
                return False
            if not runJob("%s %s xmipp_cuda_test.o -o xmipp_cuda_test -lcudart -lcufft" %
                          (self.configDict["CXX"], self.configDict["NVCC_LINKFLAGS"])):
                print(red("Check the CXX and NVCC_LINKFLAGS"))
                return False
            runJob("rm xmipp_cuda_test*")
        return True

    def _config_MPI(self):
        mpiBinCandidates = [os.environ.get('MPI_BINDIR', 'None'),
                            '/usr/lib/openmpi/bin',
                            '/usr/lib64/openmpi/bin']
        if self.configDict["MPI_RUN"] == "":
            if checkProgram("mpirun", False):
                self.configDict["MPI_RUN"] = "mpirun"
                print(green("'mpirun' detected."))
            elif checkProgram("mpiexec", False):
                self.configDict["MPI_RUN"] = "mpiexec"
                print(green("'mpiexec' detected."))
            else:
                print(yellow("\n'mpirun' and 'mpiexec' not found in the PATH"))
                mpiDir = findFileInDirList('mpirun', mpiBinCandidates)
                mpiDir = askPath(mpiDir, self.ask)
                if mpiDir:
                    self.configDict["MPI_RUN"] = os.path.join(mpiDir, "mpirun")
                    checkProgram(self.configDict["MPI_RUN"])
                    self.environment.update(PATH=mpiDir)
        if self.configDict["MPI_CC"] == "":
            if checkProgram("mpicc", False):
                self.configDict["MPI_CC"] = "mpicc"
                print(green("'mpicc' detected."))
            else:
                print(yellow("\n'mpicc' not found in the PATH"))
                mpiDir = findFileInDirList('mpicc', mpiBinCandidates)
                mpiDir = askPath(mpiDir, self.ask)
                if mpiDir:
                    self.configDict["MPI_CC"] = os.path.join(mpiDir, "mpicc")
                    checkProgram(self.configDict["MPI_CC"])
        if self.configDict["MPI_CXX"] == "":
            if checkProgram("mpicxx", False):
                self.configDict["MPI_CXX"] = "mpicxx"
                print(green("'mpicxx' detected."))
            else:
                print(yellow("\n'mpicxx' not found in the PATH"))
                mpiDir = findFileInDirList('mpicxx', mpiBinCandidates)
                mpiDir = askPath(mpiDir, self.ask)
                if mpiDir:
                    self.configDict["MPI_CXX"] = os.path.join(mpiDir, "mpicxx")
                    checkProgram(self.configDict["MPI_CXX"])

        mpiLib_env = os.environ.get('MPI_LIBDIR', '')
        if mpiLib_env:
            self.configDict['MPI_CXXFLAGS'] += ' -L'+mpiLib_env

        mpiInc_env = os.environ.get('MPI_INCLUDE', '')
        if mpiInc_env:
            self.configDict['MPI_CXXFLAGS'] += ' -I'+mpiInc_env

        if self.configDict["MPI_LINKERFORPROGRAMS"] == "":
            self.configDict["MPI_LINKERFORPROGRAMS"] = self.configDict["MPI_CXX"]

    def _check_MPI(self):
        print("Checking MPI configuration ...")
        cppProg = """
    #include <mpi.h>
    int main(){}
    """
        with open("xmipp_mpi_test_main.cpp", "w") as cppFile:
            cppFile.write(cppProg)

        if not runJob("%s -c -w %s %s %s xmipp_mpi_test_main.cpp -o xmipp_mpi_test_main.o"
                      % (self.configDict["MPI_CXX"], self.configDict["INCDIRFLAGS"],
                         self.configDict["CXXFLAGS"], self.configDict["MPI_CXXFLAGS"])):
            print(red(
                "MPI compilation failed. Check the INCDIRFLAGS, MPI_CXX and CXXFLAGS in 'xmipp.conf'"))
            print(red("In addition, MPI_CXXFLAGS can also be used to add flags to MPI compilations."
                      "'%s --showme:compile' might help" % self.configDict['MPI_CXX']))
            return False

        libhdf5 = self._get_Hdf5_name(self.configDict["LIBDIRFLAGS"])
        if not runJob("%s %s %s %s xmipp_mpi_test_main.o -o xmipp_mpi_test_main "
                      "-lfftw3 -lfftw3_threads -l%s  -lhdf5_cpp -ltiff -ljpeg -lsqlite3 -lpthread"
                      % (self.configDict["MPI_LINKERFORPROGRAMS"], self.configDict["LINKFLAGS"],
                         self.configDict["MPI_LINKFLAGS"], self.configDict["LIBDIRFLAGS"], libhdf5)):
            print(red("Check the LINKERFORPROGRAMS, LINKFLAGS and LIBDIRFLAGS"))
            print(red("In addition, MPI_LINKFLAGS can also be used to add flags to MPI links. "
                      "'%s --showme:compile' might help" % self.configDict['MPI_CXX']))
            return False
        runJob("rm xmipp_mpi_test_main*")

        echoString = blue(
            "   > This sentence should be printed 2 times if mpi runs fine")
        if not (runJob("%s -np 2 echo '%s.'" % (self.configDict['MPI_RUN'], echoString)) or
                runJob("%s -np 2 --allow-run-as-root echo '%s.'" % (self.configDict['MPI_RUN'], echoString))):
            print(red("mpirun or mpiexec have failed."))
            return False
        return True

    def _config_Java(self):
        if self.configDict["JAVA_HOME"] == "":
            javaProgramPath = whereis('javac', findReal=True)
            if not javaProgramPath:
                print(yellow("\n'javac' not found in the PATH"))
                javaProgramPath = findFileInDirList(
                    'javac', ['/usr/lib/jvm/java-*/bin'])  # put candidates here
                javaProgramPath = askPath(javaProgramPath, self.ask)
            if not os.path.isdir(javaProgramPath):
                installDepConda('openjdk')
                javaProgramPath = whereis('javac', findReal=True)

            if javaProgramPath:
                self.environment.update(PATH=javaProgramPath)
                javaHomeDir = javaProgramPath.replace("/jre/bin", "")
                javaHomeDir = javaHomeDir.replace("/bin", "")
                self.configDict["JAVA_HOME"] = javaHomeDir

        if self.configDict["JAVA_BINDIR"] == "" and self.configDict["JAVA_HOME"]:
            self.configDict["JAVA_BINDIR"] = "%(JAVA_HOME)s/bin"
        if self.configDict["JAVAC"] == "" and self.configDict["JAVA_HOME"]:
            self.configDict["JAVAC"] = "%(JAVA_BINDIR)s/javac"
        if self.configDict["JAR"] == "" and self.configDict["JAVA_HOME"]:
            self.configDict["JAR"] = "%(JAVA_BINDIR)s/jar"
        if self.configDict["JNI_CPPPATH"] == "" and self.configDict["JAVA_HOME"]:
            self.configDict["JNI_CPPPATH"] = "%(JAVA_HOME)s/include:%(JAVA_HOME)s/include/linux"

        if (os.path.isfile((self.configDict["JAVAC"] % self.configDict) % self.configDict) and
                os.path.isfile((self.configDict["JAR"] % self.configDict) % self.configDict) and
                os.path.isdir("%(JAVA_HOME)s/include" % self.configDict)):
            print(green("Java detected at: %s" % self.configDict["JAVA_HOME"]))
        else:
            print(red("No development environ for 'java' found. "
                      "Please, check JAVA_HOME, JAVAC, JAR and JNI_CPPPATH variables."))

    def _check_Java(self):
        if not checkProgram(self.configDict['JAVAC']):
            return False
        print("Checking Java configuration...")
        javaProg = """
        public class Xmipp {
        public static void main(String[] args) {}
        }
    """
        with open("Xmipp.java", "w") as javaFile:
            javaFile.write(javaProg)
        if not runJob("%s Xmipp.java" % self.configDict["JAVAC"]):
            print(red("Check the JAVAC"))
            return False
        runJob("rm Xmipp.java Xmipp.class")

        cppProg = """
    #include <jni.h>
    int dummy(){}
    """
        with open("xmipp_jni_test.cpp", "w") as cppFile:
            cppFile.write(cppProg)

        incs = ""
        for x in self.configDict['JNI_CPPPATH'].split(':'):
            incs += " -I"+x
        if not runJob("%s -c -w %s %s xmipp_jni_test.cpp -o xmipp_jni_test.o" %
                      (self.configDict["CXX"], incs, self.configDict["INCDIRFLAGS"])):
            print(red("Check the JNI_CPPPATH, CXX and INCDIRFLAGS"))
            return False
        runJob("rm xmipp_jni_test*")
        return True