function [wave_out,user_config,state] = process(wave_in,signal_dimensions,user_config,state)
%PROCESS
% Function takes a 2 channel signal fragment and switches the channels,
% applying a gain
wave_out = user_config(1).value(1,1) * fliplr(wave_in);
end
