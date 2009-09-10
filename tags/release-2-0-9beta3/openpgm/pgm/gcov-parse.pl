#!/usr/bin/perl

$type = '';
$target = '';

while (<>) {
	chomp;
	if (/^(Function|File) '(.+)'/) {
		$type = $1;
		$target = $2;
	} elsif (/^Lines executed:(\d+\.\d+)% of (\d+)/) {
#		print "$type,$target,$1,$2\n";
		if ($type cmp 'File') {
			$files{$target} = $1;
		} else {
			$functions{$target} = $1;
		}
	}
}

#@sorted = sort { $files{$a} <=> $files{$b} } keys %files;
#foreach $name (@sorted)
#{
#	print "$name:$files{$name}\n";
#}
@sorted = sort { $functions{$a} <=> $functions{$b} } keys %functions;
$total = 0;
$count = 0;
foreach $name (@sorted)
{
	next if $name =~ m#^/#;
	next if $name =~ m#.+\.h$#;
	next if $name =~ m#_unittest\.c$#;
	print sprintf("%20s: %3.1f%%\n", $name, $functions{$name});
	$total += $functions{$name};
	$count++;
}
$total /= $count;
print "\n               TOTAL: ~" . int($total) . "%\n\n";

# eof
