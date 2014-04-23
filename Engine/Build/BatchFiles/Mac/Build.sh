#!/bin/sh

# This script gets called every time Xcode does a build or clean operation, even though it's called "Build.sh".
# Values for $ACTION: "" = building, "clean" = cleaning

# Setup Mono
source Engine/Build/BatchFiles/Mac/SetupMono.sh Engine/Build/BatchFiles/Mac

case $ACTION in
	"")
		echo "Building $1..."
		xbuild /property:Configuration=Development /verbosity:quiet /nologo Engine/Source/Programs/UnrealBuildTool/UnrealBuildTool_Mono.csproj |grep -i error

                Platform=""
                AdditionalFlags="-deploy"
		                
                if [ $2 = "iphoneos" ]
                then
	                Platform="IOS"
			AdditionalFlags+=" -nocreatestub"
                else
	                if [ $2 = "iphonesimulator" ]
	                then
		                Platform="IOS"
		                AdditionalFlags+=" -simulator"
				AdditionalFlags+=" -nocreatestub"
	                else
		                Platform="Mac"
	                fi
                fi

		mono Engine/Binaries/DotNET/UnrealBuildTool.exe $1 $Platform $3 $AdditionalFlags "$4"
		;;
	"clean")
		echo "Cleaning $2 $3 $4..."
		xbuild /property:Configuration=Development /verbosity:quiet /nologo Engine/Source/Programs/UnrealBuildTool/UnrealBuildTool_Mono.csproj |grep -i error

                Platform=""
                AdditionalFlags="-clean"
		                
                if [ $3 = "iphoneos" ]
                then
	                Platform="IOS"
			AdditionalFlags+=" -nocreatestub"
                else
	                if [ $3 = "iphonesimulator" ]
	                then
		                Platform="IOS"
		                AdditionalFlags+=" -simulator"
				AdditionalFlags+=" -nocreatestub"
	                else
		                Platform="Mac"
	                fi
                fi

		mono Engine/Binaries/DotNET/UnrealBuildTool.exe $2 $Platform $4 $AdditionalFlags "$5"
		;;
esac

exit $?

