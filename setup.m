clc;
mex -DDBCC_VERSION="\"v1.1.1\"" ./*.c -output dbc;
d = dbc('Y2018_CGEA1.3_CMDB_B_v18.02_HS1.dbc');