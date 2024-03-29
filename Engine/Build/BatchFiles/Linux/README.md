Build scripts for native Linux build
====================================

This document describes how to build Unreal Engine 4.6 natively on a Linux host. 
The steps are described here are applicable to the current build, but you may
want to visit https://wiki.unrealengine.com/Building_On_Linux for the
latest updates on the process.

If you are stuck at some point, we suggest searching AnswerHub 
(https://answers.unrealengine.com/questions/topics/linux.html) for possible answers 
or asking a new question on there if you can not find what you are looking for. 
You may also receive help on #UE4Linux IRC channel on FreeNode, however it is 
not an official support outlet.


Prerequisites
-------------

The packages that are required to build the engine vary from distribution to distribution,
and an up-to-date list should be maintained (and installed) by Setup.sh -
feel free to suggest modifications. Automated install currently works for Debian-based
distributions only (Debian itself, (K)Ubuntu and Linux Mint).

Most important dependencies:
- mono 3.x (2.x may work, but is not recommended), including xbuild and C# compiler (*mcs), and libraries for NET 4.0 framework.
- clang 3.3 (clang 3.5.0 is also supported, but NOT clang 3.4).
- python (2 or 3) - needed for the script that downloads the binary dependencies.
- Qt(4,5) or GTK development packages to build LinuxNativeDialogs.
- SDL2 is also needed for building LinuxNativeDialogs module, but the rest
of the engine is using our own (modified) version of it from Engine/Source/ThirdParty/SDL2.

You will also need at least 20 GB of free disk space and a relatively powerful
machine.

If you want to rebuild third-party dependencies (we don't recommend doing
that any more), you will need many more development packages installed. Refer
to BuildThirdParty.sh script and specific automake/CMake scripts for each
dependency. You don't have to do that though as we supply prebuilt libraries.


Setting up/updating the sources
-------------------------------

Setup has been simplified since the previous releases, and the additional
binary files which are too large to be included into github repository are now being 
downloaded by GitDependencies tool with minimal hassle. After cloning the repository, 
you will need to run Setup.sh script which will invoke the said tool to download them.
The tool will be registered as a post-merge hook, so later updates to binary files
will be downloaded after each git pull.

How to set up the sources for building, step by step:

1. Clone EpicGames/UnrealEngine repository

    ``git clone https://github.com/EpicGames/UnrealEngine -b 4.6``
    
2. Run Setup.sh once.

    ``cd UnrealEngine``
    
    ``./Setup.sh``

    The script will try to install additional packages (for certain distributions) and download
    precompiled binaries of third party libraries. It will also build one of the libraries
    on your system (LinuxNativeDialogs or LND for short).

    You should see ** SUCCESS ** message after running this step. If you don't, take a look into
    BuildThirdParty.log located in Engine/Build/BatchFiles/Linux directory.
    
3. After the successful setup, you can generate makefiles (and CMakelists.txt).

    ``./GenerateProjectFiles.sh``

Updating the sources later can be done with git pull. The tool to download binary files will be
registered as a post-merge hook by Setup.sh, so third party libraries will be updated automatically
(if needed). If you ever need to run it directly, it can be found in Engine/Binaries/DotNET/ directory
(GitDependencies.exe, which needs to be invoked through mono).



Building and running
--------------------

GenerateProjectFiles.sh also produces both makefile and CMakeLists.txt which you can use to import the
project in your favorite IDE. KDevelop 4.6+ is known to handle the project well 
(it takes about 3-4 GB of resident RAM to load the project though).

The targets match the name of the resulting binary, e.g. UE4Editor-Linux-Debug or UE4Game. You can build them
by just typing make <target> in the engine's root folder.

Specifically, to be able to run the editor, build the following targets:

    make ShaderCompileWorker UnrealLightmass UnrealPak UE4Editor

If you intend to develop the editor, you can build a debug configuration of it:

    make UE4Editor-Linux-Debug

(note that it will still use development ShaderCompileWorker / UnrealLightmass). This
configuration runs much slower.

If you want to rebuild the editor from scatch, you can use

    make UE4Editor ARGS="-clean" && make UE4Editor

In order to run it:

    cd Engine/Binaries/Linux/
    ./UE4Editor

Or, if you want to start it with a specific project:

    cd Engine/Binaries/Linux/
    ./UE4Editor "~/Documents/Unreal Projects/MyProject/MyProject.uproject"
    
You can also append -game if you want to run the project as a game (you can also do that from the running editor).

Notes
-----

Depending on mono version and some other not yet clarified circumstances (this may be relevant: 
http://stackoverflow.com/questions/13859467/ravendb-client-onlinux-connecting-to-windows-server-using-mono-http),
binary downloader tool invoked by Setup.sh may fail. In that case, Setup.sh will keep re-running it
until it succeeds (or at least stops crashing). You may want to keep an eye on this as there is
a slight possibility of the script getting stuck in an infinite loop if the tool keeps crashing.

On the first start, the editor will be "compiling shaders" (a bit misleading terminology, 
it will be converting them to GLSL). The result will be stored in Engine/DerivedDataCache folder 
and used for subsequent runs.

Depending on the project, the editor may need rather large number of file handles 
(e.g. 16000+). If you start seeing errors about not being able to open files, 
you may need to adjust your limits.

It is advised that you install the editor on a case insensitive filesystem if you
intend to open projects from other systems (OS X and Windows), or Marketplace.

The time it takes to build the editor in development configuration can be large,
debug configuration takes about 2/3 of this time. The build process can also take 
significant amount of RAM (roughly 1GB per core).

It is also possible to cross-compile the editor (currently from Windows only), 
although you will still need to build LinuxNativeDialogs on the Linux machine. 
You may use this route if your Windows machine happens to be more powerful,
but explanation of it is beyond the scope of this document.

