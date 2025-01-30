#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

source lib.sh
source net_helper.sh

readonly CLI=$(dirname $(readlink -f $0))/../../../net/ynl/pyynl/cli.py

readonly SRC=1
readonly DST=2

readonly NET_V4=192.168.1.
readonly NET_V6=2001:db8::
readonly OL1_NET_V4=172.16.1.
readonly OL1_NET_V6=2001:db8:1::
readonly OL2_NET_V4=172.16.2.
readonly OL2_NET_V6=2001:db8:2::

trap cleanup_all_ns EXIT

is_ipv6() {
	if [[ $1 =~ .*:.* ]]; then
		return 0
	fi
	return 1
}

create_gnv_endpoint() {
	local -r netns=$1
	local -r bm_dev=$2
	local -r bm_rem_addr=$3
	local -r gnv_dev=$4
	local -r gnv_id=$5
	local opts
	local rem
	shift 5

	if is_ipv6 $bm_rem_addr; then
		rem=remote6
	else
		rem=remote
	fi

	while [ -n "$1" ]; do
		opts="$opts, $1"
		shift
	done

	ip netns exec $netns $CLI --family rt_link --create --excl --do newlink  --json \
		'{"ifname": "'$gnv_dev'", "linkinfo": {"kind":"geneve", "data":
			{ "id": '$gnv_id', "'$rem'": "'$bm_rem_addr'" '"$opts"'} } }' > /dev/null
	ip -n $netns link set dev $gnv_dev up
}

create_vxlan_endpoint() {
	local -r netns=$1
	local -r bm_dev=$2
	local -r bm_rem_addr=$3
	local -r vxlan_dev=$4
	local -r vxlan_id=$5
	local opts
	shift 5

	# convert the arguments from yaml format
	while [ -n "$1" ]; do
		local opt=$1
		local pattern='"port":'

		opts="$opts ${opt/$pattern/dstport }"
		shift
	done
	[ -n "$opts" ] || opts="dstport 4789"

	ip -n $netns link add $vxlan_dev type vxlan id $vxlan_id remote $bm_rem_addr $opts
	ip -n $netns link set dev $vxlan_dev up
}

create_ns() {
	local create_endpoint
	local nested_opt="$2"
	local options="$1"
	local addr_src
	local addr_dst
	local feature
	local dev
	local id
	local ns

	RET=0

	#  +-------------+    +-------------+
	#  | NS_SRC      |    | NS_NST_DST  |
	#  |             |    |             |
	#  |   gnv_nst1  |    |  gnv_nst2   |
	#  |   +         |    |         +   |
	#  |   |         |    |         |   |
	#  |   +         |    |         +   |
	#  |  gnv1       |    |        gnv2 |
	#  |   +         |    |         +   |
	#  |   |         |    |         |   |
	#  |   + veth1 +--------+ veth2 +   |
	#  |             |    |             |
	#  +-------------+    +-------------+

	setup_ns NS_SRC NS_DST

	ip link add name veth$SRC netns $NS_SRC type veth peer name veth$DST netns $NS_DST
	case "$ENCAP" in
	vxlan)
		create_endpoint=create_vxlan_endpoint
		dev=vx
		;;
	geneve)
		create_endpoint=create_gnv_endpoint
		dev=gnv
		;;
	esac

	id=1
	for ns in ${NS_LIST[@]}; do
		ip -n $ns link set dev veth$id up

		# ensure the sender can do large write just after 3whs
		ip netns exec $ns sysctl -qw net.ipv4.tcp_wmem="4096 4194304 4194304"

		# note that 3 - $SRC == $DST and 3 - $DST == $SRC
		if [ $FAMILY = "4" ]; then
			ip -n $ns addr add dev veth$id $NET_V4$id/24
			$create_endpoint $ns veth$id $NET_V4$((3 - $id)) $dev$id 4 $options
			ip -n $ns addr add dev $dev$id $OL1_NET_V4$id/24

			# nested tunnel devices
			# pmtu can't be propagated to upper layer devices; need manual adjust
			$create_endpoint $ns $dev$id $OL1_NET_V4$((3 - $id)) $dev"_nst"$id 40 \
				'"port":6082' $nested_opt
			ip -n $ns addr add dev $dev"_nst"$id $OL2_NET_V4$id/24
			ip -n $ns link set dev $dev"_nst"$id mtu 1392
		else
			ip -n $ns addr add dev veth$id $NET_V6$id/64 nodad
			$create_endpoint $ns veth$id $NET_V6$((3 - $id)) $dev"6"$id 6 $options
			ip -n $ns addr add dev $dev"6"$id $OL1_NET_V6$id/64 nodad

			$create_endpoint $ns $dev"6"$id $OL1_NET_V6$((3 - $id)) $dev"6_nst"$id 60 \
				'"port":6082' $nested_opt
			ip -n $ns addr add dev $dev"6_nst"$id $OL2_NET_V6$id/64 nodad
			ip -n $ns link set dev $dev"6_nst"$id mtu 1352
		fi
		id=$((id+1))
	done

	# enable GRO heuristic on the veth peer and ensure UDP L4 over tunnel is
	# actually segmented
	for feature in tso tx-udp_tnl-segmentation; do
		ip netns exec $NS_SRC ethtool -K veth$SRC $feature off 2>/dev/null
	done
}

create_ns_gso()
{
	local dev

	create_ns $*
	if [ $ENCAP = "geneve" ]; then
		dev=gnv
	else
		dev=vx
	fi
	if [ $FAMILY = "4" ]; then
		ip netns exec $NS_SRC ethtool -K $dev$SRC tx-gso-partial on \
			tx-udp_tnl-segmentation on tx-udp_tnl-csum-segmentation on
	else
		ip netns exec $NS_SRC ethtool -K $dev"6"$SRC tx-gso-partial on \
			tx-udp_tnl-segmentation on tx-udp_tnl-csum-segmentation on
	fi
}

create_ns_gso_gro()
{
	create_ns_gso $*
	ip netns exec $NS_DST ethtool -K veth$DST gro on
	ip netns exec $NS_SRC ethtool -K veth$SRC tx off >/dev/null 2>&1
}

run_test() {
	local -r dst=$NET$DST
	local -r msg=$1
	local -r total_size=$2
	local -r encappkts=$3
	local rx_args=""
	local rx_family="-4"
	local filter=IpInReceives
	local ipt=iptables
	local wire_pkts
	local dport
	local pkts

	if [ $FAMILY = "6" ]; then
		# rx program does not support '-6' and implies ipv6 usage by default
		rx_family=""
		filter=Ip6InReceives
		ipt=ip6tables
	fi

	# The received can only check fixed size packet
	pkts=$((total_size / $GSO_SIZE))
	rx_args="$rx_family"
	if [ -n "$4" ]; then
		wire_pkts=$4
	elif [ $((total_size % $GSO_SIZE)) -eq 0 ]; then
		wire_pkts=1
		rx_args="$rx_args -l $GSO_SIZE"
	else
		wire_pkts=2
		pkts=$((pkts + 1))
	fi

	if [ $ENCAP = "geneve" ]; then
		dport=6081
	else
		dport=4789
	fi

	# ignore shorts packet, to avoid arp/mld induced noise
	ip netns exec $NS_SRC $ipt -A OUTPUT -p udp --dport $dport -m length --length 300:65535
	ip netns exec $NS_DST $ipt -A INPUT -p udp --dport $dport -m length --length 300:65535

	ip netns exec $NS_DST ./udpgso_bench_rx -C 2000 -t -R 100 -n $pkts $rx_args &
	local spid=$!
	wait_local_port_listen "$NS_DST" 8000 tcp
	ip netns exec $NS_SRC ./udpgso_bench_tx -$FAMILY -t -M 1 -s $total_size -D $dst
	local ret=$?
	check_err $ret "client failure exit code $ret"
	wait $spid
	ret=$?
	check_err $ret "sever failure exit code $ret"

	local snd=$(ip netns exec $NS_SRC $ipt"-save" -c |
		    grep "dport $dport" | sed -e 's/\[//' -e 's/:.*//')
	[ $snd -eq $wire_pkts ]
	check_err $? "send $snd packets on the lowest link, expected $wire_pkts"

	local rcvpkts=$(ip netns exec $NS_DST $ipt"-save" -c | \
			grep "dport $dport" | sed -e 's/\[//' -e 's/:.*//')

	[ $rcvpkts -eq $(encappkts) ]
	check_err $? "received $rcvpkts $ENCAP packets, expected $encappkts"
	log_test "$msg"
}

# tcp retransmisions will break the accounting
[ "$KSFT_MACHINE_SLOW" = yes ] && FAIL_TO_XFAIL=yes
for FAMILY in 4 6; do
	NET=$OL2_NET_V4
	IPT=iptables
	GSO_SIZE=1340 # 1392 - 20 - 32

	if [ $FAMILY = 6 ]; then
		NET=$OL2_NET_V6
		IPT=ip6tables
		GSO_SIZE=1280 # 1352 - 40 - 32
	fi

	echo "IPv$FAMILY"

	# "geneve" must be last encap in list, so that later
	# test cases will run on it
	for ENCAP in "vxlan" "geneve"; do
		create_ns
		run_test "No GSO - $ENCAP" $((GSO_SIZE * 4)) 4 4
		cleanup_all_ns

		create_ns_gso
		run_test "GSO without GRO - $ENCAP" $((GSO_SIZE * 4)) 4 1
		cleanup_all_ns

		# IPv4 only test
		[ $FAMILY = "4" ] || continue
		create_ns_gso
		ip netns exec $NS_SRC sysctl -qw net.ipv4.ip_no_pmtu_disc=1
		run_test "GSO disable due to no fixedid - $ENCAP" $((GSO_SIZE * 4)) 4 4
		cleanup_all_ns
	done

	# GRO tests imply/require geneve encap, the only one providing
	# GRO hints
	create_ns_gso_gro
	run_test "double tunnel GRO, no hints" $((GSO_SIZE * 4)) 4
	cleanup_all_ns

	create_ns_gso_gro '"gro-hint":1'
	run_test "double tunnel GRO" $((GSO_SIZE * 4)) 1
	cleanup_all_ns

	create_ns_gso_gro '"gro-hint":1,"udp-csum":1' '"udp-csum":1'
	run_test "double tunnel GRO - csum complete" $((GSO_SIZE * 4)) 1
	cleanup_all_ns

	create_ns_gso_gro '"gro-hint":1' '"udp-csum":1,"udp-zero-csum6-tx":1,"udp-zero-csum6-rx":1'
	run_test "double tunnel GRO - no nested csum" $((GSO_SIZE * 4)) 1
	cleanup_all_ns

	create_ns_gso_gro '"gro-hint":1,"udp-zero-csum6-tx":1,"udp-zero-csum6-rx":1' '"udp-csum":1'
	run_test "double tunnel GRO - skip due nested csum with outer 0-csum" $((GSO_SIZE * 4)) 4
	cleanup_all_ns

	create_ns_gso_gro '"gro-hint":1,"udp-csum":1' '"udp-csum":1 "inner-proto-inherit":1'
	run_test "double tunnel GRO - nested inherit proto" $((GSO_SIZE * 4)) 1
	cleanup_all_ns

	create_ns_gso_gro '"gro-hint":1'
	run_test "double tunnel GRO - short last pkt" $((GSO_SIZE * 4 + $GSO_SIZE / 2)) 2
	cleanup_all_ns
done

exit $EXIT_STATUS
