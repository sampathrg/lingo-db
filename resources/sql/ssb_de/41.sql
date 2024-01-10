select d_year, c_nation, sum(cast(lo_revenue as int8) - cast(lo_supplycost as int8)) as profit
from date_dict_encoded_str, customer_SSB_dict_encoded_str, supplier_dict_encoded_str, part_dict_encoded_str, lineorder
where lo_custkey = c_custkey
and lo_suppkey = s_suppkey
and lo_partkey = p_partkey
and lo_orderdate = d_datekey
and c_region = 2 -- c_region = 'AMERICA'
and s_region = 0 -- s_region = 'AMERICA'
and (p_mfgr = 0 or p_mfgr = 3) -- (p_mfgr = 'MFGR#1' or p_mfgr = 'MFGR#2') TODO - Check if this is correct.
group by d_year, c_nation
-- order by d_year, c_nation;
