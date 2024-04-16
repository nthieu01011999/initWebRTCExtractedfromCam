#!/bin/sh

# udhcpc script edited by Tim Riker <Tim@Rikers.org>

[ -z "$1" ] && echo "Error: should be called from udhcpc" && exit 1

ACTION="$1"
RESOLV_CONF="/etc/resolv.conf"
[ -e $RESOLV_CONF ] || touch $RESOLV_CONF
[ -n "$broadcast" ] && BROADCAST="broadcast $broadcast"
[ -n "$subnet" ] && NETMASK="netmask $subnet"
# Handle stateful DHCPv6 like DHCPv4
[ -n "$ipv6" ] && ip="$ipv6/128"

echo "broadcast: $broadcast"
echo "subnet: $subnet"

if [ -z "${IF_WAIT_DELAY}" ]; then
        IF_WAIT_DELAY=10
fi

wait_for_ipv6_default_route() {
        printf "Waiting for IPv6 default route to appear"
        while [ $IF_WAIT_DELAY -gt 0 ]; do
                if ip -6 route list | grep -q default; then
                        printf "\n"
                        return
                fi
                sleep 1
                printf "."
                : $((IF_WAIT_DELAY -= 1))
        done
        printf " timeout!\n"
}

case "$ACTION" in
        deconfig)
                /sbin/ifconfig $interface up
                /sbin/ifconfig $interface 0.0.0.0

                # drop info from this interface
                # resolv.conf may be a symlink to /tmp/, so take care
                TMPFILE=$(mktemp)
                grep -vE "# $interface\$" $RESOLV_CONF > $TMPFILE
                cat $TMPFILE > $RESOLV_CONF
                rm -f $TMPFILE

                if [ -x /usr/sbin/avahi-autoipd ]; then
                        /usr/sbin/avahi-autoipd -c $interface && /usr/sbin/avahi-autoipd -k $interface
                fi
                ;;

        leasefail|nak)
                if [ -x /usr/sbin/avahi-autoipd ]; then
                        /usr/sbin/avahi-autoipd -c $interface || /usr/sbin/avahi-autoipd -wD $interface --no-chroot
                fi
                ;;

        renew|bound)
                if [ -x /usr/sbin/avahi-autoipd ]; then
                        /usr/sbin/avahi-autoipd -c $interface && /usr/sbin/avahi-autoipd -k $interface
                fi
                echo "ACTION: $ACTION"
                echo "interface: $interface"
                echo "ip: $ip"
                echo "BROADCAST: $BROADCAST"
                echo "NETMASK: $NETMASK" #netmask 255.255.255.0
                echo "dns: $dns"
                /sbin/ifconfig $interface $ip $BROADCAST $NETMASK
                if [ -n "$ipv6" ] ; then
                        wait_for_ipv6_default_route
                fi
                # quangtv23 add feature set metric
                if [ "$interface" = "eth0" ]; then
                        metric=29
                elif [ "$interface" = "wlan0" ]; then
                        metric=69
                else
                        metric=0
                fi

                # RFC3442: If the DHCP server returns both a Classless
                # Static Routes option and a Router option, the DHCP
                # client MUST ignore the Router option.
                if [ -n "$staticroutes" ]; then
                        echo "deleting routers"
                        echo "staticroutes: $staticroutes"
                        route -n | while read dest gw mask flags metric ref use iface; do
                                [ "$iface" != "$interface" -o "$gw" = "0.0.0.0" ] || \
                                        route del -net "$dest" netmask "$mask" gw "$gw" dev "$interface"
                        done

                        # format: dest1/mask gw1 ... destn/mask gwn
                        set -- $staticroutes
                        while [ -n "$1" -a -n "$2" ]; do
                                route add -net "$1" gw "$2" dev "$interface" metric $metric
                                shift 2
                        done
                elif [ -n "$router" ] ; then
                        echo "deleting routers"
                        echo "router: $router"
                        while route del default gw 0.0.0.0 dev $interface 2> /dev/null; do
                                :
                        done

                        for i in $router ; do
                                echo "i: $i"
                                route add default gw $i dev $interface metric $metric
                        done
                fi

                # drop info from this interface
                # resolv.conf may be a symlink to /tmp/, so take care
                TMPFILE=$(mktemp)
                echo "TMPFILE: $TMPFILE"
                grep -vE "# $interface\$" $RESOLV_CONF > $TMPFILE
                cat $TMPFILE > $RESOLV_CONF
                rm -f $TMPFILE

                # prefer rfc3397 domain search list (option 119) if available
                if [ -n "$search" ]; then
                        search_list=$search
                elif [ -n "$domain" ]; then
                        search_list=$domain
                fi

                [ -n "$search_list" ] &&
                        echo "search $search_list # $interface" >> $RESOLV_CONF

                for i in $dns ; do
                        echo adding dns $i
                        echo "nameserver $i # $interface" >> $RESOLV_CONF
                done
                echo 1 > /var/run/netConnectStatus
                ;;
esac

HOOK_DIR="$0.d"
echo "HOOK_DIR: $HOOK_DIR"
for hook in "${HOOK_DIR}/"*; do
    [ -f "${hook}" -a -x "${hook}" ] || continue
    "${hook}" "$ACTION"
    echo "hook: $hook"
done

exit 0