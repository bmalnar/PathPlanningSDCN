DIRNAME=build
cd `dirname $0`
rm -rf ${DIRNAME}
mkdir -p ${DIRNAME}
cd ${DIRNAME}
cmake ..
make
