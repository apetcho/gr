#!/bin/bash

# set current working directory to the script directory
cd $( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )

if [ -z "${GRDIR}" ]; then
    GRDIR="/opt/gr"
fi
if [ -z "${DESTDIR}" ]; then
    DESTDIR="../tmp"
fi

get_first_directory() {
    local DIRPATH=${1}

    if [ "${DIRPATH:0:1}" == "/" ]; then
        DIRPATH=${DIRPATH:1}
    fi
    DIRPATH=${DIRPATH%%/*}
    if [ "${1:0:1}" == "/" ]; then
        DIRPATH="/${DIRPATH}"
    fi
    echo ${DIRPATH}
}

NAME="gr"
SRC_DIR="${DESTDIR}${GRDIR}"
VERSION=$(GRDIR=${SRC_DIR} ./_get_version.py ${SRC_DIR})
PYTHON_REL_SRC_DIR="lib/python"
BIN_DIR="/usr/bin"
LIB_DIR="/usr/lib"
INC_DIR="/usr/include"
BIN_LINKS=( "gr" "gksm" )
INC_LINKS=( "gks.h" "gr.h" "gr3.h" )
LIB_LINKS=( "libGKS.a" "libGKS.so" "libGR.a" "libGR.so" "libGR3.a" "libGR3.so" )
PY_LINKS=( "gr gr3 qtgr" )
GRDIR_FIRST_DIR="$(get_first_directory ${GRDIR})"
if [ "${GRDIR_FIRST_DIR}" == "/usr" ]; then
    PACKAGE_DIRS=( "/usr" )
else
    PACKAGE_DIRS=( "/usr" "${GRDIR_FIRST_DIR}" )
fi
if [ "${GRDIR_FIRST_DIR}" == "/usr" ]; then
    CLEANUP_DIRS=( "/usr" "/scripts" )
else
    CLEANUP_DIRS=( "/usr" "/scripts" "${GRDIR_FIRST_DIR}" )
fi
OWNED_DIRS=( "${GRDIR}" )
VALID_DISTROS=( "centos" "centos6" "suse" "debian" )


array_contains() {
    # first argument: key, other arguments: array contains (passing by ${array[@]})
    local e
    for e in "${@:2}"; do [[ "$e" == "$1" ]] && return 0; done
    return 1
}

get_dependencies() {
    local TMP_DISTRO

    if [ ! -z "${1}" ]; then
        TMP_DISTRO="${1}"
    else
        TMP_DISTRO="${DISTRO}"
    fi

    case ${TMP_DISTRO} in
    debian)
        DEPENDENCIES=( "python-numpy" )
        ;;
    centos)
        DEPENDENCIES=( "numpy" )
        ;;
    centos6)
        DEPENDENCIES=( "numpy" )
        ;;
    suse)
        DEPENDENCIES=( "python-numpy" )
        ;;
    unspecified_distro)
        if [ -f /etc/debian_version ] || [ -f /etc/SuSE-release ]; then
            get_dependencies "debian"
        else
            get_dependencies "centos"
        fi
        ;;
    *)
        echo "${DISTRO} is an invalid distribution string! => No package dependencies set!"
        ;;
    esac
}

create_directory_structure() {
    mkdir -p "$@"
}

create_directory_structure_for_unspecified_distro() {
    PYTHON_VERSION=$(python -c 'import platform; print ".".join(platform.python_version_tuple()[:2])')
    PYTHON_DIR=$(python -c 'from distutils.sysconfig import get_python_lib; print get_python_lib()')
    local DIRECTORIES=( ".${GRDIR}" ".${BIN_DIR}" ".${LIB_DIR}" ".${INC_DIR}" ".${PYTHON_DIR}" )

    create_directory_structure "${DIRECTORIES[@]}"
}

create_directory_structure_for_debian() {
    PYTHON_VERSION="2.7"
    PYTHON_DIR="/usr/lib/python${PYTHON_VERSION}/dist-packages"
    local DIRECTORIES=( ".${GRDIR}" ".${BIN_DIR}" ".${LIB_DIR}" ".${INC_DIR}" ".${PYTHON_DIR}" )

    create_directory_structure "${DIRECTORIES[@]}"
}

create_directory_structure_for_centos() {
    PYTHON_VERSION="2.7"
    PYTHON_DIR="/usr/lib/python${PYTHON_VERSION}/site-packages"
    local DIRECTORIES=( ".${GRDIR}" ".${BIN_DIR}" ".${LIB_DIR}" ".${INC_DIR}" ".${PYTHON_DIR}" )

    create_directory_structure "${DIRECTORIES[@]}"
}

create_directory_structure_for_centos6() {
    PYTHON_VERSION="2.6"
    PYTHON_DIR="/usr/lib/python${PYTHON_VERSION}/site-packages"
    local DIRECTORIES=( ".${GRDIR}" ".${BIN_DIR}" ".${LIB_DIR}" ".${INC_DIR}" ".${PYTHON_DIR}" )

    create_directory_structure "${DIRECTORIES[@]}"
}

create_directory_structure_for_suse() {
    create_directory_structure_for_centos
}

copy_src_and_setup_startup() {
    cp -r ${SRC_DIR}/* ".${GRDIR}/"
    for LINK in ${BIN_LINKS[@]}; do
        ln -s "${GRDIR}/bin/${LINK}" ".${BIN_DIR}/${LINK}"
    done
    for LINK in ${LIB_LINKS[@]}; do
        ln -s "${GRDIR}/lib/${LINK}" ".${LIB_DIR}/${LINK}"
    done
    for LINK in ${INC_LINKS[@]}; do
        ln -s "${GRDIR}/include/${LINK}" ".${INC_DIR}/${LINK}"
    done
    for LINK in ${PY_LINKS[@]}; do
        ln -s "${GRDIR}/${PYTHON_REL_SRC_DIR}/${LINK}" ".${PYTHON_DIR}/${LINK}"
    done
    python -m compileall ".${GRDIR}/${PYTHON_REL_SRC_DIR}"
}

create_package_scripts() {
    mkdir scripts
    cat >scripts/after_install.sh <<EOF
#!/bin/bash
ldconfig
EOF
    chmod +x scripts/after_install.sh 
}

create_package() {
    local DEPENDENCIES_STRING=""
    for DEP in ${DEPENDENCIES}; do
        DEPENDENCIES_STRING="${DEPENDENCIES_STRING} -d ${DEP}"
    done
    local FPM_PACKAGE_DIRS=""
    for DIR in ${PACKAGE_DIRS[@]}; do
        FPM_PACKAGE_DIRS="${FPM_PACKAGE_DIRS} .${DIR}"
    done
    
    fpm -s dir -t "${PACKAGE_FORMAT}" -n "${NAME}" -v "${VERSION}" --directories ${OWNED_DIRS[@]} --after-install scripts/after_install.sh ${DEPENDENCIES_STRING} ${FPM_PACKAGE_DIRS}

    if [ "${DISTRO}" != "unspecified_distro" ]; then
        mkdir "${DISTRO}"
        mv *.${PACKAGE_FORMAT} "${DISTRO}/"
    fi
}

create_package_for_unspecified_distro() {
    if [ -f /etc/debian_version ]; then
        PACKAGE_FORMAT="deb"
    else
        PACKAGE_FORMAT="rpm"
    fi

    create_package
}

create_package_for_debian() {
    PACKAGE_FORMAT="deb"

    create_package
}

create_package_for_centos() {
    PACKAGE_FORMAT="rpm"

    create_package
}

create_package_for_centos6() {
    create_package_for_centos
}

create_package_for_suse() {
    create_package_for_centos
}

cleanup() {
    for DIR in ${CLEANUP_DIRS[@]}; do
        rm -rf ".${DIR}"
    done
}

main() {
    DISTRO="${1}"
    if [ ! -z "${DISTRO}" ]; then
        if ! array_contains "${DISTRO}" "${VALID_DISTROS[@]}"; then
            echo "The first parameter (${DISTRO}) is no valid linux distribution name."
            exit 1
        fi
    else
        DISTRO="unspecified_distro"
    fi

    get_dependencies

    eval "create_directory_structure_for_${DISTRO}"
    copy_src_and_setup_startup
    create_package_scripts
    eval "create_package_for_${DISTRO}"

    cleanup
}

main "$@"
