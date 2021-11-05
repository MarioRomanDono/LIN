for i in $(seq 1 $1); do
	echo add $i > /proc/modlist
done
