FIX_mini
========

This is a simple implementation of the FIX protocol demonstrating sending orders, canceling orders, replacing orders
and handling executions and 'outs' ( confirmation that a canceled order has indeed been canceled ).  There is handling for 
some additional messages.

Implementations of FIX vary by broker and exchange so you will need to adapt this to your desired target.

This implementation is not mean to demonstrate how to implement FIX in the highest performance configuration possible.

Its meant to provide a quick start to users new to FIX.

WARNING:

Always do your testing first on a test system ( hopefully your broker/exchange provides this ).  DO NOT start live!

Although this code has been tested thorouhly, the author disclaims all liability for any uses of this code.

