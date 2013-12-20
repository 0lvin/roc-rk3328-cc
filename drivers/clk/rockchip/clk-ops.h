#ifndef __RK_CLK_OPS_H
#define __RK_CLK_OPS_H
#include "clkops-dtsi.h"
struct clk_ops_table {
	unsigned int 		index;
	const struct clk_ops	*clk_ops;
};
const struct clk_ops *rk_get_clkops(u32 idx);
//#define RKCLK_DEBUG

#if defined(RKCLK_DEBUG)
#define clk_debug(fmt, args...) printk(KERN_INFO "rkclk: "fmt, ##args)
#else
#define clk_debug(fmt, args...) do {} while(0)
#endif

#define clk_err(fmt, args...) printk(KERN_ERR "rkclk: "fmt, ##args)

#endif /* __RK_CLKOPS_H */
