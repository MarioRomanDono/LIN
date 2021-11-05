for i in $(seq 1 $1); do
	echo remove $i > /proc/modlist
	cat /proc/modlist
done;
