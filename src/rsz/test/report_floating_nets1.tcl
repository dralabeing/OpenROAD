source "helpers.tcl"
# report_floating_nets
read_liberty Nangate45/Nangate45_typ.lib
read_lef Nangate45/Nangate45.lef
read_verilog report_floating_nets1.v
link_design top
report_floating_nets
report_floating_nets -verbose
