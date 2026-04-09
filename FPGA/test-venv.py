import os
from amaranth import *
from amaranth.build import *
from amaranth.vendor import LatticeICE40Platform

class MinimalTest(Elaboratable):
  def elaborate(self, platform):
    topModule = Module()
    testCounter = Signal(24)
    
    topModule.d.sync += testCounter.eq(testCounter + 1)
    
    testLed = platform.request("led", 0)
    topModule.d.comb += testLed.o.eq(testCounter[23])
    
    return topModule

class TargetUP5KPlatform(LatticeICE40Platform):
  device = "iCE40UP5K"
  package = "sg48"
  default_clk = "clk"
  resources = [
    Resource("clk", 0, Pins("35", dir="i"), Clock(12e6)),
    Resource("led", 0, Pins("39", dir="o"))
  ]
  connectors = []

if (__name__ == "__main__"):
  os.environ["AMARANTH_USE_YOWASP"] = "1"
  
  targetPlatform = TargetUP5KPlatform()
  targetPlatform.build(MinimalTest(), do_program=False)

