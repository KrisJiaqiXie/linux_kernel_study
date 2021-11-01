/*
 *
 * Header for code common to all OMAP1 machines.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * You should have received a copy of the  GNU General Public License along
 * with this program; if not, write  to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __ARCH_ARM_MACH_OMAP1_COMMON_H
#define __ARCH_ARM_MACH_OMAP1_COMMON_H

#include <plat/common.h>

#if defined(CONFIG_ARCH_OMAP730) || defined(CONFIG_ARCH_OMAP850)
void omap7xx_map_io(void);
#else
static inline void omap7xx_map_io(void)
{
}
#endif

#ifdef CONFIG_ARCH_OMAP15XX
void omap15xx_map_io(void);
#else
static inline void omap15xx_map_io(void)
{
}
#endif

#ifdef CONFIG_ARCH_OMAP16XX
void omap16xx_map_io(void);
#else
static inline void omap16xx_map_io(void)
{
}
#endif

void omap1_init_early(void);
void omap1_init_irq(void);
void omap1_restart(char, const char *);

extern struct sys_timer omap1_timer;
extern bool omap_32k_timer_init(void);
extern void __init omap_init_consistent_dma_size(void);

#endif /* __ARCH_ARM_MACH_OMAP1_COMMON_H */
