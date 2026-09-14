/* clang-format off */
#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>
#include "xdp_shared.h"
/* clang-format on */

struct {
    __uint(type, BPF_MAP_TYPE_XSKMAP);
    __uint(max_entries, 64);
    __type(key, __u32);
    __type(value, __u32);
} sockets SEC(".maps");
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, struct encos_xdp_filter);
} filters SEC(".maps");

SEC("xdp") int ethernet_redirect(struct xdp_md* ctx) {
    void* end = (void*) (long) ctx->data_end;
    struct ethhdr* eth = (void*) (long) ctx->data;
    if ((void*) (eth + 1) > end || eth->h_proto != bpf_htons(ETH_P_IP))
        return XDP_PASS;
    struct iphdr* ip = (void*) (eth + 1);
    if ((void*) (ip + 1) > end || ip->version != 4 || ip->ihl != 5 || ip->protocol != 17 ||
        (ip->frag_off & bpf_htons(0x3fff)))
        return XDP_PASS;
    struct udphdr* udp = (void*) (ip + 1);
    if ((void*) (udp + 1) > end)
        return XDP_PASS;
    __u32 zero = 0;
    struct encos_xdp_filter* filter = bpf_map_lookup_elem(&filters, &zero);
    if (!filter || ctx->rx_queue_index != filter->queue ||
        (filter->remote_ip ? ip->saddr != filter->remote_ip
                           : (ip->saddr & bpf_htonl(0xffffff00U)) != bpf_htonl(0xc0a86400U)) ||
        ip->daddr != filter->local_ip || udp->source != filter->remote_port ||
        udp->dest != filter->local_port)
        return XDP_PASS;
    return bpf_redirect_map(&sockets, ctx->rx_queue_index, XDP_PASS);
}
char LICENSE[] SEC("license") = "GPL";
