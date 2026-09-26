#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/kobject.h>
#include <soc/samsung/cal-if.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <soc/samsung/fvmap.h>

#include "cmucal.h"
#include "vclk.h"
#include "ra.h"

#define FVMAP_SIZE		(SZ_8K)
#define STEP_UV			(6250)

void __iomem *fvmap_base;
void __iomem *sram_fvmap_base;

static int init_margin_table[MAX_MARGIN_ID];
static int percent_margin_table[MAX_MARGIN_ID];

static int margin_mif;
static int margin_int;
static int margin_cpucl0;
static int margin_cpucl1;
static int margin_g3d;
static int margin_cam;
static int margin_disp;
static int margin_aud;
static int margin_cp;

static int volt_offset_percent;

module_param(margin_mif, int, 0);
module_param(margin_int, int, 0);
module_param(margin_cpucl0, int, 0);
module_param(margin_cpucl1, int, 0);
module_param(margin_g3d, int, 0);
module_param(margin_cam, int, 0);
module_param(margin_disp, int, 0);
module_param(margin_aud, int, 0);
module_param(margin_cp, int, 0);

module_param(volt_offset_percent, int, 0);

void margin_table_init(void)
{
	init_margin_table[MARGIN_MIF] = margin_mif;
	init_margin_table[MARGIN_INT] = margin_int;
	init_margin_table[MARGIN_CPUCL0] = margin_cpucl0;
	init_margin_table[MARGIN_CPUCL1] = margin_cpucl1;
	init_margin_table[MARGIN_G3D] = margin_g3d;
	init_margin_table[MARGIN_CAM] = margin_cam;
	init_margin_table[MARGIN_DISP] = margin_disp;
	init_margin_table[MARGIN_AUD] = margin_aud;
	init_margin_table[MARGIN_CP] = margin_cp;
}

int fvmap_set_raw_voltage_table(unsigned int id, int uV)
{
	struct fvmap_header *fvmap_header;
	struct rate_volt_header *fv_table;
	int num_of_lv;
	int idx, i;

	idx = GET_IDX(id);

	fvmap_header = sram_fvmap_base;
	fv_table = sram_fvmap_base + fvmap_header[idx].o_ratevolt;
	num_of_lv = fvmap_header[idx].num_of_lv;

	for (i = 0; i < num_of_lv; i++)
		fv_table->table[i].volt += uV;

	return 0;
}

static int get_vclk_id_from_margin_id(int margin_id)
{
	int size = cmucal_get_list_size(ACPM_VCLK_TYPE);
	int i;
	struct vclk *vclk;

	for (i = 0; i < size; i++) {
		vclk = cmucal_get_node(ACPM_VCLK_TYPE | i);

		if (vclk->margin_id == margin_id)
			return i;
	}

	return -EINVAL;
}

#define attr_percent(margin_id, type)								\
static ssize_t show_##type##_percent								\
(struct kobject *kobj, struct kobj_attribute *attr, char *buf)					\
{												\
	return snprintf(buf, PAGE_SIZE, "%d\n", percent_margin_table[margin_id]);		\
}												\
												\
static ssize_t store_##type##_percent								\
(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)		\
{												\
	int input, vclk_id;									\
												\
	if (!sscanf(buf, "%d", &input))								\
		return -EINVAL;									\
												\
	if (input < -100 || input > 100)							\
		return -EINVAL;									\
												\
	vclk_id = get_vclk_id_from_margin_id(margin_id);					\
	if (vclk_id == -EINVAL)									\
		return vclk_id;									\
	percent_margin_table[margin_id] = input;						\
	cal_dfs_set_volt_margin(vclk_id | ACPM_VCLK_TYPE, input);				\
												\
	return count;										\
}												\
												\
static struct kobj_attribute type##_percent =							\
__ATTR(type##_percent, 0600,									\
	show_##type##_percent, store_##type##_percent)

attr_percent(MARGIN_MIF, mif_margin);
attr_percent(MARGIN_INT, int_margin);
attr_percent(MARGIN_CPUCL0, cpucl0_margin);
attr_percent(MARGIN_CPUCL1, cpucl1_margin);
attr_percent(MARGIN_G3D, g3d_margin);
attr_percent(MARGIN_CAM, cam_margin);
attr_percent(MARGIN_DISP, disp_margin);
attr_percent(MARGIN_AUD, aud_margin);
attr_percent(MARGIN_CP, cp_margin);

static struct attribute *percent_margin_attrs[] = {
	&mif_margin_percent.attr,
	&int_margin_percent.attr,
	&cpucl0_margin_percent.attr,
	&cpucl1_margin_percent.attr,
	&g3d_margin_percent.attr,
	&cam_margin_percent.attr,
	&disp_margin_percent.attr,
	&aud_margin_percent.attr,
	&cp_margin_percent.attr,
	NULL,
};

static const struct attribute_group percent_margin_group = {
	.attrs = percent_margin_attrs,
};

int fvmap_get_freq_volt_table(unsigned int id, void *freq_volt_table, unsigned int table_size)
{
	struct fvmap_header *fvmap_header = fvmap_base;
	struct rate_volt_header *fv_table;
	int idx, i, j;
	int num_of_lv;
	struct freq_volt *table = (struct freq_volt *)freq_volt_table;

	if (!IS_ACPM_VCLK(id))
		return -EINVAL;

	if (!table)
		return -ENOMEM;

	idx = GET_IDX(id);

	fvmap_header = fvmap_base;
	fv_table = fvmap_base + fvmap_header[idx].o_ratevolt;
	num_of_lv = fvmap_header[idx].num_of_lv;

	for (i = 0; i < table_size; i++) {
		for (j = 0; j < num_of_lv; j++) {
			if (fv_table->table[j].rate == table[i].rate)
				table[i].volt = fv_table->table[j].volt;
		}

		if (table[i].volt == 0) {
			if (table[i].rate > fv_table->table[0].rate)
				table[i].volt = fv_table->table[0].volt;
			else if (table[i].rate < fv_table->table[num_of_lv - 1].rate)
				table[i].volt = fv_table->table[num_of_lv - 1].volt;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(fvmap_get_freq_volt_table);

#if 0
int fvmap_get_raw_voltage_table(unsigned int id)
{
	struct fvmap_header *fvmap_header;
	struct rate_volt_header *fv_table;
	int idx, i;
	int num_of_lv;
	unsigned int table[20];

	idx = GET_IDX(id);

	fvmap_header = sram_fvmap_base;
	fv_table = sram_fvmap_base + fvmap_header[idx].o_ratevolt;
	num_of_lv = fvmap_header[idx].num_of_lv;

	for (i = 0; i < num_of_lv; i++)
		table[i] = fv_table->table[i].volt * STEP_UV;

	for (i = 0; i < num_of_lv; i++)
		printk("dvfs id : %d  %d Khz : %d uv\n", ACPM_VCLK_TYPE | id, fv_table->table[i].rate, table[i]);

	return 0;
}
#endif

int fvmap_get_voltage_table(unsigned int id, unsigned int *table)
{
	struct fvmap_header *fvmap_header = fvmap_base;
	struct rate_volt_header *fv_table;
	int idx, i;
	int num_of_lv;

	if (!IS_ACPM_VCLK(id))
		return 0;

	idx = GET_IDX(id);

	fvmap_header = fvmap_base;
	fv_table = fvmap_base + fvmap_header[idx].o_ratevolt;
	num_of_lv = fvmap_header[idx].num_of_lv;

	for (i = 0; i < num_of_lv; i++)
		table[i] = fv_table->table[i].volt;

	return num_of_lv;

}
EXPORT_SYMBOL_GPL(fvmap_get_voltage_table);

static void fvmap_copy_from_sram(void __iomem *map_base, void __iomem *sram_base)
{
	struct fvmap_header *fvmap_header, *header;
	struct rate_volt_header *old, *new;
	struct clocks *clks;
	struct pll_header *plls;
	struct vclk *vclk;
	unsigned int member_addr;
	unsigned int blk_idx;
	int size, margin;
	int i, j;
	void __iomem *header_base;

	fvmap_header = map_base;
	header = vmalloc(FVMAP_SIZE);
	memcpy(header, sram_base, FVMAP_SIZE);
	header_base = (void __iomem *)header;

	size = cmucal_get_list_size(ACPM_VCLK_TYPE);

	for (i = 0; i < size; i++) {
		/* load fvmap info */
		fvmap_header[i].dvfs_type = header[i].dvfs_type;
		fvmap_header[i].num_of_lv = header[i].num_of_lv;
		fvmap_header[i].num_of_members = header[i].num_of_members;
		fvmap_header[i].num_of_pll = header[i].num_of_pll;
		fvmap_header[i].num_of_mux = header[i].num_of_mux;
		fvmap_header[i].num_of_div = header[i].num_of_div;
		fvmap_header[i].gearratio = header[i].gearratio;
		fvmap_header[i].init_lv = header[i].init_lv;
		fvmap_header[i].num_of_gate = header[i].num_of_gate;
		fvmap_header[i].reserved[0] = header[i].reserved[0];
		fvmap_header[i].reserved[1] = header[i].reserved[1];
		fvmap_header[i].block_addr[0] = header[i].block_addr[0];
		fvmap_header[i].block_addr[1] = header[i].block_addr[1];
		fvmap_header[i].block_addr[2] = header[i].block_addr[2];
		fvmap_header[i].o_members = header[i].o_members;
		fvmap_header[i].o_ratevolt = header[i].o_ratevolt;
		fvmap_header[i].o_tables = header[i].o_tables;

		vclk = cmucal_get_node(ACPM_VCLK_TYPE | i);
		if (vclk == NULL)
			continue;
		pr_info("dvfs_type : %s - id : %x\n",
			vclk->name, fvmap_header[i].dvfs_type);
		pr_info("  num_of_lv      : %d\n", fvmap_header[i].num_of_lv);
		pr_info("  num_of_members : %d\n", fvmap_header[i].num_of_members);

		old = header_base + fvmap_header[i].o_ratevolt;
		new = map_base + fvmap_header[i].o_ratevolt;

		margin = init_margin_table[vclk->margin_id];
		if (margin)
			cal_dfs_set_volt_margin(i | ACPM_VCLK_TYPE, margin);

		if (volt_offset_percent) {
			if ((volt_offset_percent < 100) && (volt_offset_percent > -100)) {
				if (i < MAX_MARGIN_ID) {
					percent_margin_table[i] = volt_offset_percent;
					cal_dfs_set_volt_margin(i | ACPM_VCLK_TYPE, volt_offset_percent);
				}
			}
		}

		for (j = 0; j < fvmap_header[i].num_of_members; j++) {
			if (i == 8)	break;
			clks = header_base + fvmap_header[i].o_members;

			if (j < fvmap_header[i].num_of_pll) {
				plls = header_base + clks->addr[j];
				member_addr = plls->addr - 0x90000000;
			} else {

				member_addr = (clks->addr[j] & ~0x3) & 0xffff;
				blk_idx = clks->addr[j] & 0x3;


				member_addr |= ((fvmap_header[i].block_addr[blk_idx]) << 16) - 0x90000000;
			}


			vclk->list[j] = cmucal_get_id_by_addr(member_addr);

			if (vclk->list[j] == INVALID_CLK_ID)
				pr_info("  Invalid addr :0x%x\n", member_addr);
			else
				pr_info("  DVFS CMU addr:0x%x\n", member_addr);
		}

		for (j = 0; j < fvmap_header[i].num_of_lv; j++) {
			new->table[j].rate = old->table[j].rate;
			new->table[j].volt = old->table[j].volt;
			pr_info("  lv : [%7d], volt = %d uV (%d %%) \n",
				new->table[j].rate, new->table[j].volt,
				volt_offset_percent);
		}
	}

	vfree((void *)header);
}

#define OC_PLL_M_STOCK		255
#define OC_PLL_M_MAX		276
#define OC_VOLT_MAX_UV		1350000
#define OC_VOLT_MIN_UV		600000
#define OC_MAX_LEVEL		32

struct oc_cluster {
	int idx;
	struct cmucal_pll *pll;
	void __iomem *pll_hdr;
	void __iomem *pms0;
	void __iomem *volt0;
	u32 orig_pms0;
	u32 orig_volt0;
	bool volt_steps;
};

static struct oc_cluster oc_cl[2] = { { .idx = -1 }, { .idx = -1 } };
static DEFINE_MUTEX(oc_lock);

static u32 oc_field(u32 v, unsigned char shift, unsigned char width)
{
	return (v >> shift) & width_to_mask(width);
}

static bool oc_pms_is_stock(struct oc_cluster *c)
{
	struct cmucal_pll *pll = c->pll;

	if (!pll || !pll->m_width)
		return false;

	return oc_field(c->orig_pms0, pll->m_shift, pll->m_width) == OC_PLL_M_STOCK &&
	       oc_field(c->orig_pms0, pll->p_shift, pll->p_width) == 3 &&
	       oc_field(c->orig_pms0, pll->s_shift, pll->s_width) == 0;
}

static unsigned int oc_volt_to_uv(struct oc_cluster *c, u32 raw)
{
	return c->volt_steps ? raw * STEP_UV : raw;
}

static void cpu_oc_setup(void __iomem *sram_base)
{
	struct fvmap_header *fh = fvmap_base;
	int size = cmucal_get_list_size(ACPM_VCLK_TYPE);
	struct cmucal_clk *clk;
	struct oc_cluster *c;
	struct vclk *vclk;
	u16 hdr_off, level;
	u8 lv0_idx;
	int i;

	for (i = 0; i < size; i++) {
		vclk = cmucal_get_node(ACPM_VCLK_TYPE | i);
		if (!vclk || (vclk->margin_id != MARGIN_CPUCL0 && vclk->margin_id != MARGIN_CPUCL1))
			continue;
		if (!fh[i].num_of_pll || !vclk->num_list || fh[i].o_members & 1 ||
		    fh[i].o_members + 2 > FVMAP_SIZE)
			continue;

		hdr_off = readw(sram_base + fh[i].o_members);
		if ((hdr_off | fh[i].o_ratevolt) & 3 || hdr_off + 8 > FVMAP_SIZE)
			continue;

		level = readw(sram_base + hdr_off + 6);
		if (!level || level > OC_MAX_LEVEL ||
		    hdr_off + 8 + level * 4 > FVMAP_SIZE ||
		    fh[i].o_ratevolt + fh[i].num_of_lv * 8 > FVMAP_SIZE ||
		    fh[i].o_tables + fh[i].num_of_lv * fh[i].num_of_members > FVMAP_SIZE)
			continue;

		c = &oc_cl[vclk->margin_id - MARGIN_CPUCL0];
		if (c->idx >= 0)
			continue;
		c->idx = i;
		c->pll_hdr = sram_base + hdr_off;
		lv0_idx = readb(sram_base + fh[i].o_tables);
		if (lv0_idx < level)
			c->pms0 = c->pll_hdr + 8 + lv0_idx * 4;

		c->volt0 = sram_base + fh[i].o_ratevolt + 4;
		c->orig_volt0 = readl(c->volt0);
		c->volt_steps = c->orig_volt0 < 1000;

		clk = cmucal_get_node(vclk->list[0]);
		if (clk && IS_PLL(clk->id))
			c->pll = to_pll_clk(clk);

		if (c->pms0)
			c->orig_pms0 = readl(c->pms0);

		pr_info("cpu_oc: %s idx %d hdr 0x%x level %u lv0_idx %u pms0 0x%08x volt0 %u\n",
			vclk->name, i, hdr_off, level, lv0_idx, c->orig_pms0, c->orig_volt0);
	}
}

static ssize_t oc_fvmap_show(struct oc_cluster *c, char *buf)
{
	struct fvmap_header *fh = fvmap_base;
	void __iomem *sram = sram_fvmap_base;
	struct cmucal_pll *pll = c->pll;
	u16 level, members;
	u32 v;
	int n = 0, i, j;

	if (c->idx < 0)
		return -ENODEV;

	fh += c->idx;
	members = fh->num_of_members;
	level = min_t(u16, readw(c->pll_hdr + 6), OC_MAX_LEVEL);

	n += scnprintf(buf + n, PAGE_SIZE - n,
		       "idx %d lv %u mem %u pll %u mux %u div %u o_mem 0x%x o_rv 0x%x o_tbl 0x%x\n",
		       c->idx, fh->num_of_lv, members, fh->num_of_pll, fh->num_of_mux,
		       fh->num_of_div, fh->o_members, fh->o_ratevolt, fh->o_tables);
	n += scnprintf(buf + n, PAGE_SIZE - n, "pll addr 0x%08x o_lock 0x%x level %u shift m%u p%u s%u\n",
		       readl(c->pll_hdr), readw(c->pll_hdr + 4), level,
		       pll ? pll->m_shift : 0, pll ? pll->p_shift : 0, pll ? pll->s_shift : 0);

	for (i = 0; i < level; i++) {
		v = readl(c->pll_hdr + 8 + i * 4);
		n += scnprintf(buf + n, PAGE_SIZE - n, "pms[%2d] 0x%08x", i, v);
		if (pll && pll->m_width)
			n += scnprintf(buf + n, PAGE_SIZE - n, " m %u p %u s %u",
				       oc_field(v, pll->m_shift, pll->m_width),
				       oc_field(v, pll->p_shift, pll->p_width),
				       oc_field(v, pll->s_shift, pll->s_width));
		n += scnprintf(buf + n, PAGE_SIZE - n, "\n");
	}

	for (i = 0; i < fh->num_of_lv; i++) {
		n += scnprintf(buf + n, PAGE_SIZE - n, "lv[%2d] %7u %7u tbl",
			       i, readl(sram + fh->o_ratevolt + i * 8),
			       readl(sram + fh->o_ratevolt + i * 8 + 4));
		for (j = 0; j < members; j++)
			n += scnprintf(buf + n, PAGE_SIZE - n, " %u",
				       readb(sram + fh->o_tables + i * members + j));
		n += scnprintf(buf + n, PAGE_SIZE - n, "\n");
	}

	return n;
}

static ssize_t oc_pll_m_show(struct oc_cluster *c, char *buf)
{
	struct cmucal_pll *pll = c->pll;

	if (!c->pms0 || !oc_pms_is_stock(c))
		return -ENODEV;

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 oc_field(readl(c->pms0), pll->m_shift, pll->m_width));
}

static ssize_t oc_pll_m_store(struct oc_cluster *c, const char *buf, size_t count)
{
	struct cmucal_pll *pll = c->pll;
	unsigned int m;
	u32 v;

	if (!c->pms0 || !oc_pms_is_stock(c))
		return -ENODEV;
	if (kstrtouint(buf, 0, &m) || m < OC_PLL_M_STOCK || m > OC_PLL_M_MAX)
		return -EINVAL;

	mutex_lock(&oc_lock);
	v = c->orig_pms0 & ~get_mask(pll->m_width, pll->m_shift);
	v |= m << pll->m_shift;
	writel(v, c->pms0);
	mutex_unlock(&oc_lock);

	return count;
}

static ssize_t oc_pms0_raw_show(struct oc_cluster *c, char *buf)
{
	if (!c->pms0)
		return -ENODEV;

	return scnprintf(buf, PAGE_SIZE, "0x%08x orig 0x%08x\n", readl(c->pms0), c->orig_pms0);
}

static ssize_t oc_pms0_raw_store(struct oc_cluster *c, const char *buf, size_t count)
{
	u32 v;

	if (!c->pms0)
		return -ENODEV;
	if (kstrtou32(buf, 0, &v))
		return -EINVAL;

	mutex_lock(&oc_lock);
	writel(v, c->pms0);
	mutex_unlock(&oc_lock);

	return count;
}

static ssize_t oc_volt0_show(struct oc_cluster *c, char *buf)
{
	if (c->idx < 0)
		return -ENODEV;

	return scnprintf(buf, PAGE_SIZE, "%u orig %u\n",
			 oc_volt_to_uv(c, readl(c->volt0)), oc_volt_to_uv(c, c->orig_volt0));
}

static ssize_t oc_volt0_store(struct oc_cluster *c, const char *buf, size_t count)
{
	unsigned int uv;

	if (c->idx < 0)
		return -ENODEV;
	if (kstrtouint(buf, 0, &uv) || uv < OC_VOLT_MIN_UV || uv > OC_VOLT_MAX_UV)
		return -EINVAL;

	mutex_lock(&oc_lock);
	writel(c->volt_steps ? DIV_ROUND_UP(uv, STEP_UV) : uv, c->volt0);
	mutex_unlock(&oc_lock);

	return count;
}

#define oc_attr_ro(cl, name)								\
static ssize_t cpucl##cl##_##name##_show						\
(struct kobject *kobj, struct kobj_attribute *attr, char *buf)				\
{											\
	return oc_##name##_show(&oc_cl[cl], buf);					\
}											\
static struct kobj_attribute cpucl##cl##_##name = __ATTR(cpucl##cl##_##name, 0444,	\
	cpucl##cl##_##name##_show, NULL)

#define oc_attr_rw(cl, name)								\
static ssize_t cpucl##cl##_##name##_show						\
(struct kobject *kobj, struct kobj_attribute *attr, char *buf)				\
{											\
	return oc_##name##_show(&oc_cl[cl], buf);					\
}											\
static ssize_t cpucl##cl##_##name##_store						\
(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)	\
{											\
	return oc_##name##_store(&oc_cl[cl], buf, count);				\
}											\
static struct kobj_attribute cpucl##cl##_##name = __ATTR(cpucl##cl##_##name, 0644,	\
	cpucl##cl##_##name##_show, cpucl##cl##_##name##_store)

oc_attr_ro(0, fvmap);
oc_attr_ro(1, fvmap);
oc_attr_rw(0, pll_m);
oc_attr_rw(1, pll_m);
oc_attr_rw(0, pms0_raw);
oc_attr_rw(1, pms0_raw);
oc_attr_rw(0, volt0);
oc_attr_rw(1, volt0);

static struct attribute *cpu_oc_attrs[] = {
	&cpucl0_fvmap.attr,
	&cpucl1_fvmap.attr,
	&cpucl0_pll_m.attr,
	&cpucl1_pll_m.attr,
	&cpucl0_pms0_raw.attr,
	&cpucl1_pms0_raw.attr,
	&cpucl0_volt0.attr,
	&cpucl1_volt0.attr,
	NULL,
};

static const struct attribute_group cpu_oc_group = {
	.attrs = cpu_oc_attrs,
};

int fvmap_init(void __iomem *sram_base)
{
	void __iomem *map_base;
	struct kobject *kobj;

	map_base = kzalloc(FVMAP_SIZE, GFP_KERNEL);

	fvmap_base = map_base;
	sram_fvmap_base = sram_base;
	pr_info("%s:fvmap initialize %p\n", __func__, sram_base);
	margin_table_init();
	fvmap_copy_from_sram(map_base, sram_base);

	/* percent margin for each doamin at runtime */
	kobj = kobject_create_and_add("percent_margin", kernel_kobj);
	if (!kobj)
		pr_err("Fail to create percent_margin kboject\n");

	if (sysfs_create_group(kobj, &percent_margin_group))
		pr_err("Fail to create percent_margin group\n");

	cpu_oc_setup(sram_base);
	kobj = kobject_create_and_add("cpu_oc", kernel_kobj);
	if (!kobj || sysfs_create_group(kobj, &cpu_oc_group))
		pr_err("Fail to create cpu_oc group\n");

	return 0;
}
EXPORT_SYMBOL_GPL(fvmap_init);

MODULE_LICENSE("GPL");
