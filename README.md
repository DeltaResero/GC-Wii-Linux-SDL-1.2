***
**_GameCube/Wii Linux Simple DirectMedia Layer (SDL) 1.2.y Port_**
***

This repository contains various GameCube/Wii Linux ported versions of SDL 1.2.y.
The original port can be found here: http://www.gc-linux.org/wiki/SDL_for_GC-Linux  
<br>
With this, it is possible to obtain the correct colors within SDL console based applications without Farter's framebuffer patch.  This is mostly useful for those using the XFree86 Cube (Xorg) driver as it only has correct output for X11 applications.

A full copy of this repository can be downloaded by using git (recommended) or by using the GitHub "Download ZIP" button.
To reduce the size of the download significantly, a shallow clone can be performed by adding "--depth 1" to the end of clone command, but this will make the history inaccessible and prevent pushing to the repository. Retrieving the entire repository is usually only needed for developing so it's recommended to only clone required branches rather the whole repository. For those who require a copy of the whole repository, it can be retrieved by entering the following command:

    git clone https://github.com/DeltaResero/GC-Wii-Linux-SDL-1.2.git

<br>
To clone only a standalone copy of a branch, either download directly from GitHub from within the matching branch or releases section, or clone the branch with the following command (requires Git version 1.8.X or greater):

    git clone -b BRANCH_NAME_HERE --single-branch https://github.com/DeltaResero/GC-Wii-Linux-SDL-1.2.git

Replace BRANCH_NAME_HERE with the name of the branch to be copied  
(usually in the format of: release-1.2.y-*)
<br>
<br>
For those who are using a version of Git prior to 1.8.X on Debian/Ubuntu based operating systems should able to easily update to at least an 1.8.X version via ppa.  The alternative is to compile Git from source (https://github.com/git/git).  By default any Linux operating system prior to Debian 7 and Ubuntu 13.10 use a version of Git that will require updating.  For Debian based systems, use the following commands to update Git (assuming git is already installed):

    sudo add-apt-repository ppa:pdoes/ppa
    sudo apt-get update
    sudo apt-get install git

Check which version of git is installed with the command: "git --version".  
<br>

These ported version of SDL should compile as mainline SDL does.  These are meant to be compiled on the target system (not cross compiled).  Remember to set the library paths to the correct targets if any on the system vary from the source default.  For more information on building SDL, see the various readme and help files in the various branches of this repository.
