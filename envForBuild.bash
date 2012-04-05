# current oracle env seems to have what it takes!
source /afs/slac/g/lcls/tools/oracle/oracleSetup.bash

# now add on stuff for ProC
export ORACLE_HOME=/afs/slac/package/oracle/d/linux/11.1.0
export PATH=$ORACLE_HOME/bin:${PATH}
export LD_LIBRARY_PATH=$ORACLE_HOME/precomp/lib:$LD_LIBRARY_PATH
export LD_LIBRARY_PATH=$ORACLE_HOME/lib:$LD_LIBRARY_PATH

