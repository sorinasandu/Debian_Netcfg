Template: netcfg/get_domain
Type: string
Description: Choose the domain name.
 The domain name is the part of your Internet address to the right
 of your host name.  It is often something that ends in .com, .net,
 .edu, or .org.  If you are setting up a home network, you can make
 something up, but make sure you use the same domain name on all
 your computers. 
Description-ru: Выберите имя домена.
 Имя домена - это часть вашего Интернет-адреса, справа от имени хоста.
 Зачастую она заканчивается на .com, .net, .edu или .org. Если вы
 устанавливаете домашнюю сеть, то вы можете указать что-нибудь свое, но
 будьте уверены, что используете одинаковое имя домена на всех ваших машинах.

Template: netcfg/get_nameservers
Type: string
Description-ru: Выберите адреса серверов DNS.
 Пожалуйста, введите IP адреса (не хостовые имена) до 3 серверов имен,
 разделенные пробелами. Не используйте запятые. Сервера будут опрашиваться в
 порядке их указания. Если вы вообще не хотите использовать никакие севера имен,
 то оставьте поле пустым.

Template: netcfg/choose_interface
Type: select
Choices: ${ifchoices}
Description: Choose an interface.
 The following interfaces were detected. Choose the type of your primary
 network interface that you will need for installing the Debian system (via NFS
 or HTTP).
Choices-ru: ${ifchoices}
Description-ru: Выберите интерфейс.
 Были обнаружены следующие интерфейсы. Выберите тип вашего первичного сетевого
 интерфейса, который вам нужен для установки системы Debian (по NFS или HTTP).

Template: netcfg/error_cfg
Type: note
Description: An error occured.
 Something went wrong when I tried to activate your network.  
Description-ru: Произошла ошибка.
 Что-то пошло не так, когда я попытался активизировать вашу сеть.

Template: netcfg/get_hostname
Type: string
Default: debian
Description: Enter the system's hostname.
 The hostname is a single word that identifies your system to the
 network.  If you don't know what your hostname should be, consult
 your network administrator.  If you are setting up your own home
 network, you can make something up here. 
Description-ru: Введите системное имя хоста.
 Имя хоста - это одно слово, которое идентифицирует вашу систему в
 сети. Если вы не знаете каким должно быть имя вашей системы, то
 посоветуйтесь с вашим сетевым администратором. Если вы устанавливаете
 вашу собственную домашнюю сеть, то введите что-либо по вашему вкусу.

Template: netcfg/error
Type: note
Description: An error occured and I cannot continue.
 Feel free to retry.
Description-ru: Произошла ошибка, и я не могу продолжить.
 Вы можете попробовать повторить.

Template: netcfg/no_interfaces
Type: note 
Description: No interfaces were detected.
 No network interfaces were found.   That means that the installation system
 was unable to find a network device.  If you do have a network card, then it
 is possible that the module for it hasn't been selected yet.  Go back to 
 'Configure Network Hardware'.
Description-ru: Интерфейсы не обнаружены.
 Сетевые интерфейсы не обнаружены. Это означает, что система установки не
 смогла найти вашу сетевую карту. Если у вас есть сетевая карта, то возможно,
 что для нее пока не был выбран модуль. Вернитесь назад к пункту
 'Configure Network Hardware'.
